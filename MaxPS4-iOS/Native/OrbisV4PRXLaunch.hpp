#pragma once
#include "OrbisPRXFEXBridge.hpp"
#include "maxps4_fex_runtime.h"
#include "OrbisV4Startup.hpp"
#include "OrbisV4Imports.hpp"
#include "OrbisV4SELF.hpp"

namespace maxps4 {
// Native admission only: no FEX/JIT dependency or dispatch. The owner is
// published only after metadata, complete instruction stream and stack pass.
struct OrbisV4PRXLaunch {
    static constexpr uint64_t stack = 0x700000000000ULL;
    static int prepare(const maxps4_v4_prx_request& request,
                       OrbisV4Translation::Invalidator invalidate,
                       std::unique_ptr<OrbisV4Translation>& output,
                       std::string* diagnostic=nullptr) {
        output.reset();
        if (diagnostic) diagnostic->clear();
        auto fail=[&](int status,const std::string& reason) {
            if (diagnostic) *diagnostic=reason;
            return status;
        };
        if (!request.file || !request.length || request.length>32*1024*1024 ||
            !request.instruction_budget || request.instruction_budget>64)
            return fail(-EINVAL,"V4.REQUEST: bytes or instruction budget invalid (1..64)");
        const bool imports=request.abi==MAXPS4_V4_ABI_IMPORT_THREAD_SELF ||
                           request.abi==MAXPS4_V4_ABI_IMPORT_CLOCK_GETRES;
        const bool startup=request.abi==MAXPS4_V4_ABI_STARTUP_RETURN || imports;
        if (request.abi!=MAXPS4_V4_ABI_SYNTHETIC_HLT &&
            request.abi!=MAXPS4_V4_ABI_STATIC_EXEC_HLT && !startup)
            return fail(-ENOTSUP,"V4.ABI: unsupported ABI selector");
        OrbisPRXFEXBridge bridge;
        if (imports) {
            std::string reason;
            const int status=OrbisV4Imports::prepare(request,bridge,reason);
            if (status) return fail(status,reason);
        } else if (request.abi==MAXPS4_V4_ABI_STATIC_EXEC_HLT || startup) {
            // Separate opt-in contract: no PS4 process ABI is implied. The
            // existing linker rejects containers, unknown headers and TLS.
            OrbisLinker::Image image;
            std::string linkerDiagnostic;
            std::vector<uint8_t> canonical;
            const uint8_t* linkBytes=request.file; size_t linkSize=request.length;
            if (request.length>=4 && OrbisMetadata::read(request.file,4)==0x1d3d154f) {
                std::string reason;
                const int status=OrbisV4SELF::adapt(request.file,request.length,canonical,reason);
                if (status) return fail(status,reason);
                linkBytes=canonical.data(); linkSize=canonical.size();
            }
            const std::vector<OrbisLinker::Input> inputs{
                {{{"homebrew.elf",{}},linkBytes,linkSize},0}};
            const std::vector<OrbisLinker::HLE> services;
            if (request.load_bias) return fail(-ENOTSUP,"V4.BIAS: static entry requires zero load bias");
            if (!OrbisLinker::link(inputs,services,image,linkerDiagnostic))
                return fail(-ENOTSUP,"V4.LINK: "+linkerDiagnostic);
            if (image.modules.size()!=1 || image.modules.front().image.type!=0xfe00 ||
                image.modules.front().image.requiresLinker)
                return fail(-ENOTSUP,"V4.FORMAT: standalone static ET_SCE_EXEC required; dynamic startup absent");
            if (image.modules.front().entry!=request.entry)
                return fail(-ENOTSUP,"V4.ENTRY: request differs from ELF entry");
            if (!bridge.map(image)) return fail(-ENOTSUP,"V4.MAP: guest mapping/permissions/collision refused");
        } else {
            OrbisPRXPreparation prepared;
            if (!prepared.prepare({{"v4.prx",{request.file,request.file+request.length},request.load_bias}}))
                return fail(-ENOTSUP,"V4.PRX: metadata or preparation unsupported");
            const auto& metadata=prepared.modules.front().metadata;
            if (metadata.init || metadata.fini || !metadata.needed.empty() ||
                std::any_of(metadata.modules.begin(),metadata.modules.end(),[](const auto& m) { return !m.exported; }) ||
                std::any_of(metadata.libraries.begin(),metadata.libraries.end(),[](const auto& l) { return !l.exported; }) ||
                std::any_of(metadata.symbols.begin(),metadata.symbols.end(),[](const auto& s) {
                    return !s.defined && !s.encoded.empty();
                })) return fail(-ENOTSUP,"V4.PRX: imports/dependencies/init/fini unsupported");
            if (!bridge.map(prepared)) return fail(-ENOTSUP,"V4.MAP: PRX mapping refused");
        }
        auto pending=bridge.make_translation(std::move(invalidate));
        if (startup) {
            const int status=OrbisV4Startup::initialize(*pending);
            if (status) return fail(status,"V4.STARTUP: stack/arguments/return mapping failed");
        }
        if (!imports) {
            auto lease=pending->lease();
            uint64_t rip=request.entry, pointer=startup ? OrbisV4Startup::arguments : 0;
            bool pointerSet=startup, terminated=false;
            for (uint32_t i=0;i<request.instruction_budget;++i) {
                uint8_t op=0;
                if (lease.memory().read(rip,&op,1,true))
                    return fail(-ENOTSUP,
                        "V4.FETCH: entry/instruction not executable at RIP="+
                        std::to_string(rip)+
                        " instruction="+std::to_string(i));
                size_t length=0;
                if ((op==0xf4 && !startup) || (op==0xc3 && startup)) { terminated=true; break; }
                if (op==0x90) length=1;
                else if (op==0x64 &&
                         request.abi==MAXPS4_V4_ABI_STARTUP_RETURN) {
                    const uint8_t expected_old[8] = {
                        0x64,0x8b,0x04,0x25,
                        0xc0,0xff,0xff,0xff
                    };
                    const uint8_t expected_multi[8] = {
                        0x64,0x8b,0x04,0x25,
                        0x80,0xff,0xff,0xff
                    };
                    uint8_t actual[8]{};
                    if (lease.memory().read(rip,actual,sizeof(actual),true))
                        return fail(-ENOTSUP,
                                    "V4.TLS: unsupported FS instruction");
                    const bool old_tls = std::equal(
                        std::begin(actual),std::end(actual),
                        std::begin(expected_old));
                    const bool multi_tls = std::equal(
                        std::begin(actual),std::end(actual),
                        std::begin(expected_multi));
                    if (!old_tls && !multi_tls)
                        return fail(-ENOTSUP,
                                    "V4.TLS: unsupported FS instruction");
                    length=sizeof(actual);
                }
                else if (op==0xb8 || op==0x05) length=5;
                else if (op==0x48) {
                    uint8_t prefix[4];
                    if (startup && !lease.memory().read(rip,prefix,sizeof(prefix),true) &&
                        prefix[1]==0x8b && prefix[2]==0x7f && prefix[3]==8) {
                        uint64_t next=0;
                        if (!pointerSet || pointer>UINT64_MAX-8 ||
                            lease.memory().read(pointer+8,&next,8))
                            return fail(-ENOTSUP,"V4.OPERAND: argv pointer not readable");
                        pointer=next; length=4;
                    } else {
                        uint8_t bytes[10];
                        if (lease.memory().read(rip,bytes,sizeof(bytes),true) || bytes[1]!=0xbf)
                            return fail(-ENOTSUP,"V4.INSTRUCTION: unsupported REX instruction");
                        pointer=0;
                        for (unsigned j=0;j<8;++j) pointer|=uint64_t(bytes[2+j])<<(8*j);
                        pointerSet=true; length=10;
                    }
                } else if (op==0x8b || op==0x03) { // MOV/ADD EAX, read-only guest operand.
                    uint8_t bytes[6]{}; uint32_t data=0;
                    if (lease.read_data(rip,bytes,2,true))
                        return fail(-ENOTSUP,"V4.FETCH: truncated load instruction");
                    uint64_t address=pointer;
                    if (bytes[1]==0x05 && startup) { // RIP-relative disp32, not a host pointer.
                        if (rip>UINT64_MAX-6 || lease.read_data(rip,bytes,6,true))
                            return fail(-ENOTSUP,"V4.FETCH: truncated RIP-relative operand");
                        uint32_t raw=0;
                        for (unsigned j=0;j<4;++j) raw|=uint32_t(bytes[2+j])<<(8*j);
                        const uint64_t next=rip+6;
                        if (raw&0x80000000U) {
                            const uint64_t magnitude=0x100000000ULL-raw;
                            if (next<magnitude)
                                return fail(-ENOTSUP,"V4.OPERAND: RIP-relative underflow");
                            address=next-magnitude;
                        } else {
                            if (next>UINT64_MAX-raw)
                                return fail(-ENOTSUP,"V4.OPERAND: RIP-relative overflow");
                            address=next+raw;
                        }
                        length=6;
                    } else if (bytes[1]==0x07 && pointerSet) length=2;
                    else return fail(-ENOTSUP,"V4.OPERAND: unsupported load addressing mode");
                    if (lease.read_data(address,&data,sizeof(data)))
                        return fail(-ENOTSUP,"V4.OPERAND: load requires a proven readable guest address");
                } else return fail(-ENOTSUP,"V4.INSTRUCTION: unsupported opcode "+std::to_string(op)+
                                  " at guest "+std::to_string(rip)+" (no syscalls/branches/TLS/stack mutation)");
                uint8_t checked[10];
                if (lease.memory().read(rip,checked,length,true) || rip>UINT64_MAX-length)
                    return fail(-ENOTSUP,"V4.FETCH: truncated instruction or address overflow");
                rip+=length;
            }
            if (!terminated) return fail(-ENOTSUP,"V4.BUDGET: required RET/HLT not reached within instruction budget");
        }
        const auto page=pending->page_size();
        int status=startup ? 0 : pending->reserve(stack,page);
        if (!status && !startup) status=pending->map(stack,page,6);
        if (status) return fail(status,"V4.STACK: stack mapping failed");
        output=std::move(pending);
        if (diagnostic) *diagnostic="V4.READY: admitted experimental subset; no CPU execution by preflight";
        return 0;
    }
};
}
