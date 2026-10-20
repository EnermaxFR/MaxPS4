#pragma once
#include "OrbisTLSPlan.hpp"
#include "OrbisV4TLSMulti.hpp"

namespace maxps4 {
struct OrbisV4TLSRelocations : OrbisTLSPlan {
    // The lease pins storage throughout dispatch. Returns guest addresses only;
    // this bounded helper is not the game's __tls_get_addr ABI.
    static int resolve(const OrbisV4Translation::Lease& lease,const OrbisV4TLSMulti::Thread& thread,
                       uint32_t module,size_t offset,size_t count,uint64_t& address) {
        if(!count)return -EINVAL;
        if(thread.storage.fs_base<thread.storage.tls_begin ||
           thread.storage.fs_base-thread.storage.tls_begin>thread.storage.mapped_size)return -EFAULT;
        const uint64_t extent=thread.storage.fs_base-thread.storage.tls_begin;
        for(const auto& placement:thread.placements)
            if(placement.offset>extent || placement.size>extent-placement.offset)return -EFAULT;
        for(const auto& placement:thread.placements)if(placement.id==module) {
            if(offset>placement.size || count>placement.size-offset ||
                thread.storage.tls_begin>UINT64_MAX-placement.offset ||
                thread.storage.tls_begin+placement.offset>UINT64_MAX-offset)return -EFAULT;
            const uint64_t guest=thread.storage.tls_begin+placement.offset+offset;
            if(!lease.permitted(guest,count,6) || lease.permitted(guest,count,1))return -EFAULT;
            address=guest;return 0;
        }
        return -ENOENT;
    }
};
}
