#pragma once
#include "maxps4_orbis_m1.h"
#include "maxps4_fex_runtime.h"
namespace maxps4 {
inline int m1_collision_diagnostic(maxps4_m1_collision_report& report) {
    report={};
    report.candidate_status=report.controlled_direct_status=report.translated_status=-ECANCELED;
    GuestAddressSpace candidate(GuestAddressSpace::Backend::fex_direct);
    report.page_size=candidate.page_size();
    if (!report.page_size) return -ENOTSUP;
    report.candidate_guest=OrbisM1::thread_base+0x4000000;
    report.candidate_status=candidate.reserve(report.candidate_guest,0x10000);
    report.candidate_host_status=candidate.reservation_failure().host_status;
    candidate.clear(); // Own reservation only; never dereference existing memory.
    GuestAddressSpace blocker;
    constexpr uint64_t logical=0x2000000000ULL, marker=0xcafebabe;
    int status=blocker.reserve(logical,report.page_size);
    if (!status) status=blocker.map(logical,report.page_size,6,&marker,8);
    if (status) return status;
    const auto* host=blocker.translate(logical,8,4);
    if (!host) return -EFAULT;
    const uint64_t occupied=reinterpret_cast<uintptr_t>(host);
    GuestAddressSpace direct(GuestAddressSpace::Backend::fex_direct), translated;
    report.controlled_direct_status=direct.reserve(occupied,report.page_size);
    report.translated_status=translated.reserve(occupied,report.page_size);
    if (!report.translated_status) report.translated_status=translated.map(occupied,report.page_size,6,&marker,8);
    uint64_t observed=0;
    report.marker_preserved=!blocker.read(logical,&observed,8) && observed==marker &&
        direct.allocated()==0 && direct.regions().empty();
    if (!report.translated_status) {
        const auto* mapped=translated.translate(occupied,8,4);
        if (!mapped || mapped==host) return -EFAULT;
        status=translated.release(occupied);
        if (!status) status=translated.reserve(occupied,report.page_size);
        if (!status) status=translated.map(occupied,report.page_size,6,&marker,8);
        report.recreated=!status;
        if (status) return status;
    }
    return 0;
}
}
