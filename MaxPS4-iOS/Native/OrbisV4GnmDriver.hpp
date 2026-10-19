#pragma once
#include "OrbisV4VirtualPRX.hpp"
#include <array>

namespace maxps4 {
// Partial compatibility provider, not a GPU. Command construction is CPU work;
// submission/queue operations explicitly fail. Void doorbells, guest shaders,
// resources and all other unimplemented imports remain unresolved.
struct OrbisV4GnmDriver {
    static constexpr const char* path="libSceGnmDriver.prx";
    static constexpr uint64_t bias=0x1a00000;
    static constexpr uint32_t submission_unavailable=0x80d110ff,
        queue_unavailable=0x80d170ff,capture_unavailable=0x80d15001;
    static constexpr std::array<const char*,16> nids{
        "GGsn7jMTxw4", // DrawIndexAuto: seven DWORDs, flags=0, aligned output.
        "b08AgtPlHPg", // AreSubmitsAllowed: false (backend unavailable).
        "TLV4mswiZ4A", // DriverCaptureInProgress: false.
        "R6z1xM3pW-w", // DriverTraceInProgress: false.
        "jg33rEKLfVs", // IsUserPaEnabled: false.
        "f33OrruQYbM", // RazorIsLoaded: false.
        "rTIV11nMQuM", // ValidateOnSubmitEnabled: false.
        "d88anrgNoKY", // DriverTriggerCapture: Razor not loaded.
        "yvZ73uQUqrk", // SubmitDone: submission internal failure.
        "zwY0YV91TTI", // SubmitCommandBuffers.
        "jRcI8VcgTz4", // SubmitCommandBuffersForWorkload.
        "xbxNatawohc", // SubmitAndFlipCommandBuffers.
        "Ga6r7H6Y0RI", // SubmitAndFlipCommandBuffersForWorkload.
        "29oKvKXzEZo", // MapComputeQueue: queue internal failure.
        "A+uGq+3KFtQ", // MapComputeQueueWithPriority.
        "ArSg-TGinhk"  // UnmapComputeQueue: no queue exists.
    };
    static std::vector<uint8_t> provider() {
        std::vector<uint8_t> code;std::vector<size_t> invalid;
        auto emit=[&](std::initializer_list<uint8_t> v){code.insert(code.end(),v);};
        auto imm=[&](uint32_t v){for(unsigned i=0;i<4;++i)code.push_back(uint8_t(v>>(8*i)));};
        auto reject=[&](uint8_t cc){emit({0x0f,cc});invalid.push_back(code.size());imm(0);};
        auto result=[&](uint32_t v){const auto p=code.size();emit({0xb8});imm(v);emit({0xc3});return p;};
        emit({0x48,0x85,0xff});reject(0x84); // null
        emit({0xf7,0xc7});imm(3);reject(0x85); // unaligned
        emit({0x83,0xfe,7});reject(0x85); // uint32 size
        emit({0x85,0xc9});reject(0x85); // no predication/Neo flags in this profile
        emit({0x48,0x89,0xf8,0x48,0x83,0xc0,27});reject(0x82); // overflow
        // Probe read/write permission for every DWORD without changing its bits.
        // Translation leases pin mappings during dispatch. If a later probe
        // faults, all buffer contents remain unchanged. No host pointer escape.
        for(uint8_t off=0;off<28;off+=4)emit({0xf0,0x83,0x4f,off,0}); // LOCK OR [RDI+off],0
        for(uint8_t off=0;off<28;off+=4){
            if(off==4){emit({0x89,0x57,4});continue;} // index_count
            emit({0xc7,0x47,off});
            imm(off==0 ? 0xc0012d00U : off==8 ? 2U : off==12 ? 0xc0021000U : 0U);
        }
        emit({0x31,0xc0,0xc3});
        const auto failure=result(0xffffffffU);
        for(auto p:invalid){const auto v=uint32_t(failure-p-4);
            for(unsigned i=0;i<4;++i)code[p+i]=uint8_t(v>>(8*i));}
        const auto no=result(0),capture=result(capture_unavailable),
            submit=result(submission_unavailable),queue=result(queue_unavailable);
        std::vector<OrbisV4VirtualPRX::Export> exports{{nids[0],0,failure+6}};
        for(size_t i=1;i<nids.size();++i){
            const auto p=i<=6 ? no : i==7 ? capture : i<=12 ? submit : queue;
            exports.push_back({nids[i],p,6});
        }
        return OrbisV4VirtualPRX::build("libSceGnmDriver",code,4,exports);
    }
};
}
