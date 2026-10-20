#pragma once
#include "maxps4_orbis_v4_translation.h"
#include <array>

namespace maxps4 {
// Explicit function-entry subset, not the general PS4 process/CRT ABI.
struct OrbisV4Startup {
    static constexpr uint64_t stack=0x700000000000ULL;
    static constexpr uint64_t arguments=stack+0x100, name=stack+0x180;
    static constexpr uint64_t return_pc=stack+0x20000;
    static uint64_t rsp(size_t page) { return stack+page-8; }
    static int initialize(OrbisV4Translation& owner) {
        const auto page=owner.page_size();
        if(page<0x200 || page>return_pc-stack || (page&15))return -EINVAL;
        int status=owner.reserve(stack,page);
        if(status)return status;
        status=owner.reserve(return_pc,page);
        if(status){owner.release(stack);return status;}
        status=owner.map(stack,page,6);
        // argc, argv[0], argv terminator, envp terminator, AT_NULL pair.
        const std::array<uint64_t,6> args{1,name,0,0,0,0};
        const char arg0[]="homebrew";
        const uint64_t target=return_pc;
        if (!status) status=owner.write_span(arguments,args.data(),sizeof(args));
        if (!status) status=owner.write_span(name,arg0,sizeof(arg0));
        if (!status) status=owner.write_span(rsp(page),&target,sizeof(target));
        const uint8_t halt=0xf4;
        if (!status) status=owner.map(return_pc,page,5,&halt,1);
        if(status){owner.release(return_pc);owner.release(stack);}
        return status;
    }
};
}
