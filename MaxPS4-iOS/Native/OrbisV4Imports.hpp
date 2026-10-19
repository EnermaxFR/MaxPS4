#pragma once
#include "OrbisPRXFEXBridge.hpp"
#include "OrbisV4Startup.hpp"
#include "maxps4_fex_runtime.h"

namespace maxps4 {
// One explicit virtual HLE module, not a replacement SCE linker. The existing
// OrbisLinker parses/resolves/relocates both files and checks the closed graph.
struct OrbisV4Imports {
    static constexpr uint64_t provider_bias=0x1000000, thunk=provider_bias+0x4000;
    static constexpr uint32_t token=0x56340001;
    static constexpr const char* nid="aI+OeCz8xrQ";
    static constexpr const char* clock_nid="wRYVA5Zolso";
    static constexpr const char* provider_path="v4-libkernel.prx";
    static std::vector<uint8_t> provider(bool clock=false) {
        std::vector<uint8_t> b(0xa00,0);
        auto put=[&](size_t p,uint64_t value,size_t n=8) {
            for (size_t j=0;j<n;++j) b.at(p+j)=uint8_t(value>>(8*j));
        };
        const uint8_t magic[]={0x7f,'E','L','F',2,1,1,9,0};
        std::copy(std::begin(magic),std::end(magic),b.begin());
        put(16,0xfe18,2); put(18,62,2); put(20,1,4); put(32,64);
        put(52,64,2); put(54,56,2); put(56,4,2);
        auto ph=[&](size_t i,uint32_t type,uint32_t flags,uint64_t offset,uint64_t address,uint64_t size) {
            const auto p=64+i*56; put(p,type,4); put(p+4,flags,4); put(p+8,offset);
            put(p+16,address); put(p+32,size); put(p+40,size); put(p+48,1);
        };
        ph(0,1,5,0x200,0x4000,8); ph(1,1,6,0x300,0x8000,8);
        ph(2,2,4,0x400,0,0x100); ph(3,0x61000000,4,0x600,0,0x400);
        const uint8_t code[]={0xb8,1,0,0x34,0x56,0x0f,0x05,0xc3};
        std::copy(std::begin(code),std::end(code),b.begin()+0x200);
        put(0x201,clock ? token+1 : token,4);
        const char library[]="libkernel";
        std::copy(std::begin(library),std::end(library),b.begin()+0x601);
        size_t d=0x400;
        auto tag=[&](uint64_t t,uint64_t v) { put(d,t); put(d+8,v); d+=16; };
        tag(0x61000035,0); tag(0x61000037,11); tag(0x61000039,0x200);
        tag(0x6100003b,24); tag(0x6100003f,24); // Null symbol only: HLE registry below owns the export.
        tag(0x61000013,1|(uint64_t{1}<<32)|(uint64_t{1}<<48));
        tag(0x6100000d,1|(uint64_t{0x100}<<32)|(uint64_t{1}<<48));
        return b;
    }
    static int prepare(const maxps4_v4_prx_request& request,OrbisPRXFEXBridge& bridge,
                       std::string& diagnostic) {
        auto refuse=[&](const std::string& reason) { diagnostic="V4.IMPORT: "+reason; return -ENOTSUP; };
        const bool clock=request.abi==MAXPS4_V4_ABI_IMPORT_CLOCK_GETRES;
        OrbisMetadata metadata;
        if (!metadata.parse(request.file,request.length)) return refuse(metadata.error);
        if ((metadata.type!=0xfe10 && metadata.type!=0xfe18) || metadata.init || metadata.fini)
            return refuse("SCE dynamic executable/PRX without init/fini required");
        const auto bytes=provider(clock);
        const std::vector<OrbisLinker::Input> inputs{
            {{{"app.prx",{provider_path}},request.file,request.length},request.load_bias},
            {{{provider_path,{}},bytes.data(),bytes.size()},provider_bias}};
        const std::vector<OrbisLinker::HLE> allowed{
            {{clock ? clock_nid : nid,"libkernel","libkernel",1,0x100,2},thunk}};
        OrbisLinker::Image image; OrbisLinker::Report report;
        std::string error;
        if (!OrbisLinker::link(inputs,allowed,image,error,32*1024*1024,&report)) return refuse(error);
        if (metadata.symbols.size()!=2 || metadata.symbols[1].defined ||
            metadata.relocations.size()!=1 || metadata.relocations[0].type!=7 ||
            metadata.relocations[0].symbol!=1 || metadata.relocations[0].addend!=0 ||
            report.imports!=1 || report.resolvedImports!=1 || report.validatedRelocations!=1)
            return refuse("exactly one function import and one zero-addend R_X86_64_JUMP_SLOT required");
        const auto app=std::find_if(image.modules.begin(),image.modules.end(),[](const auto& m) {return m.path=="app.prx";});
        if (app==image.modules.end() || app->entry!=request.entry) return refuse("ELF entry mismatch");
        if (request.instruction_budget<(clock ? 7U : 5U)) return refuse("budget below required body instructions");
        const auto* code=image.translate(request.entry,clock ? 33 : 20,5);
        const uint8_t prefix[]={0x48,0x83,0xec,8,0xff,0x15};
        const uint8_t suffix[]={0x48,0x83,0xc4,8,0x05};
        if (!clock && (!code || !std::equal(std::begin(prefix),std::end(prefix),code) ||
            !std::equal(std::begin(suffix),std::end(suffix),code+10) || code[19]!=0xc3))
            return refuse("body must be SUB RSP,8 / CALL [RIP+disp32] / ADD RSP,8 / ADD EAX,imm32 / RET");
        if (clock) {
            const uint8_t start[]={0xbf,4,0,0,0,0x48,0xbe};
            const uint8_t finish[]={0x48,0x83,0xc4,8,0x8b,0x46,8,0xc3};
            if (!code || !std::equal(std::begin(start),std::end(start),code) ||
                !std::equal(std::begin(prefix),std::end(prefix),code+15) ||
                !std::equal(std::begin(finish),std::end(finish),code+25))
                return refuse("clock body must set EDI=4/RSI=output, aligned GOT CALL, read output nanoseconds and RET");
        }
        if (request.entry>UINT64_MAX-33) return refuse("entry overflow");
        const uint32_t bits=uint32_t(OrbisMetadata::read(code+(clock ? 21 : 6),4));
        const int64_t displacement=bits&0x80000000U ? int64_t(bits)-0x100000000LL : bits;
        const uint64_t next=request.entry+(clock ? 25 : 10);
        if ((displacement<0 && next<uint64_t(-displacement)) ||
            (displacement>=0 && next>UINT64_MAX-uint64_t(displacement))) return refuse("GOT displacement overflow");
        const uint64_t slot=displacement<0 ? next-uint64_t(-displacement) : next+uint64_t(displacement);
        const auto target=metadata.relocations[0].target;
        if (target>UINT64_MAX-request.load_bias || slot!=request.load_bias+target)
            return refuse("CALL must use the relocated GOT slot");
        const auto* got=image.translate(slot,8,6);
        if (!got || image.translate(slot,8,1) || OrbisMetadata::read(got,8)!=thunk)
            return refuse("GOT must contain the allowlisted guest trampoline in RW/NX memory");
        if (clock) {
            const uint64_t output=OrbisMetadata::read(code+7,8);
            if (!output || output>UINT64_MAX-16 || output<request.load_bias ||
                !app->image.translate(output-request.load_bias,16,6) ||
                !(output+16<=slot || slot+8<=output))
                return refuse("clock output must be a complete 16-byte app RW/NX buffer excluding GOT/control storage");
        }
        if (!bridge.map(image)) return refuse("mapping collision/permissions/span refused");
        return 0;
    }
    // Invoked under the translation lease. No owner lock is reacquired here.
    static bool gate(const OrbisV4Translation::Lease& lease,uint64_t entry,uint64_t initial_rsp,
                     uint64_t rip,uint64_t syscall_token,uint64_t rsp,bool clock=false,
                     uint64_t argument0=0,uint64_t argument1=0) {
        if (rip!=thunk+5 || syscall_token!=(clock ? token+1 : token) || initial_rsp<16 ||
            rsp!=initial_rsp-16 || (rsp&15)!=8 || entry>UINT64_MAX-33) return false;
        if (clock) {
            uint64_t expected=0;
            if (argument0!=4 || lease.read_data(entry+7,&expected,8) || argument1!=expected ||
                !lease.permitted(argument1,16,6)) return false;
        }
        uint64_t return_address=0;
        return !lease.memory().read(rsp,&return_address,8) && return_address==entry+(clock ? 25 : 10);
    }
};
}
