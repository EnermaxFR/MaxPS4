#pragma once
#include "OrbisPRXFEXBridge.hpp"
#include "maxps4_fex_runtime.h"

namespace maxps4 {
// Native admission only: no FEX/JIT dependency or dispatch. The owner is
// published only after metadata, complete instruction stream and stack pass.
struct OrbisV4PRXLaunch {
    static constexpr uint64_t stack = 0x700000000000ULL;
    static int prepare(const maxps4_v4_prx_request& request,
                       OrbisV4Translation::Invalidator invalidate,
                       std::unique_ptr<OrbisV4Translation>& output) {
        output.reset();
        if (!request.file || !request.length || request.length>32*1024*1024 ||
            !request.instruction_budget || request.instruction_budget>64) return -EINVAL;
        if (request.abi!=MAXPS4_V4_ABI_SYNTHETIC_HLT) return -ENOTSUP;
        OrbisPRXPreparation prepared;
        if (!prepared.prepare({{"v4.prx",{request.file,request.file+request.length},request.load_bias}}))
            return -ENOTSUP;
        const auto& metadata=prepared.modules.front().metadata;
        if (metadata.init || metadata.fini || !metadata.needed.empty() ||
            std::any_of(metadata.modules.begin(),metadata.modules.end(),[](const auto& m) { return !m.exported; }) ||
            std::any_of(metadata.libraries.begin(),metadata.libraries.end(),[](const auto& l) { return !l.exported; }) ||
            std::any_of(metadata.symbols.begin(),metadata.symbols.end(),[](const auto& s) {
                return !s.defined && !s.encoded.empty();
            })) return -ENOTSUP;
        OrbisPRXFEXBridge bridge;
        if (!bridge.map(prepared)) return -ENOTSUP;
        auto pending=bridge.make_translation(std::move(invalidate));
        {
            auto lease=pending->lease();
            uint64_t rip=request.entry, pointer=0;
            bool pointerSet=false, terminated=false;
            for (uint32_t i=0;i<request.instruction_budget;++i) {
                uint8_t op=0;
                if (lease.memory().read(rip,&op,1,true)) return -ENOTSUP;
                size_t length=0;
                if (op==0xf4) { terminated=true; break; }
                if (op==0x90) length=1;
                else if (op==0xb8 || op==0x05) length=5;
                else if (op==0x48) {
                    uint8_t bytes[10];
                    if (lease.memory().read(rip,bytes,sizeof(bytes),true) || bytes[1]!=0xbf) return -ENOTSUP;
                    pointer=0;
                    for (unsigned j=0;j<8;++j) pointer|=uint64_t(bytes[2+j])<<(8*j);
                    pointerSet=true; length=10;
                } else if (op==0x8b) {
                    uint8_t bytes[2]; uint32_t data=0;
                    if (!pointerSet || lease.memory().read(rip,bytes,sizeof(bytes),true) || bytes[1]!=0x07 ||
                        lease.memory().read(pointer,&data,sizeof(data))) return -ENOTSUP;
                    length=2;
                } else return -ENOTSUP;
                uint8_t checked[10];
                if (lease.memory().read(rip,checked,length,true) || rip>UINT64_MAX-length) return -ENOTSUP;
                rip+=length;
            }
            if (!terminated) return -ENOTSUP;
        }
        const auto page=pending->page_size();
        int status=pending->reserve(stack,page);
        if (!status) status=pending->map(stack,page,6);
        if (status) return status;
        output=std::move(pending);
        return 0;
    }
};
}
