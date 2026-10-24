#pragma once
#include "maxps4_ps4_guest_memory.h"
#include "maxps4_ps4_abi.h"
#include "maxps4_gnm_submission_abi.h"
#include "PM4MetalDrawPlan.hpp"
#include <array>
#include <cerrno>
#include <deque>
#include <mutex>
#include <new>

namespace maxps4 {
// The only currently attachable backend is the existing native triangle
// diagnostic pipeline. Its preflight refuses all guest shader/register state.
// Queue acceptance means owned work enqueued, never GPU or display completion.
class PS4GnmQueue final {
public:
    static constexpr size_t buffer_limit=16, word_limit=65536, queue_limit=4;
    static int64_t error(uint32_t code) { return ps4::signed32(0x80d10000U|code); }
    explicit PS4GnmQueue(PS4GuestMemory& memory) : memory_(memory) {}
    int32_t backend(bool enabled) {
        std::lock_guard<std::mutex> lock(lock_);
        if (!enabled) { clear(); enabled_=false; }
        else { enabled_=true; failed_=false; }
        return 0;
    }
    int64_t allowed() {
        std::lock_guard<std::mutex> lock(lock_);
        return enabled_ && !failed_ && !sealed_ && jobs_.size()<queue_limit ? 1 : 0;
    }
    int64_t submit(uint32_t count, uint64_t addresses, uint64_t sizes,
                   uint64_t constant_addresses, uint64_t constant_sizes) {
        std::lock_guard<std::mutex> lock(lock_);
        if (!enabled_ || failed_) return error(0x10ff);
        if (sealed_ || jobs_.size()>=queue_limit || next_==UINT64_MAX) return error(0x1001);
        if (!count || count>buffer_limit || !addresses || !sizes || (addresses&7) || (sizes&3)) return error(0x1000);
        // Constant-engine packets/synchronization are not implemented. Never
        // ignore non-null CCB tables, even if they happen to contain zero sizes.
        if (constant_addresses || constant_sizes) return error(0x3fff);
        try {
            std::array<uint8_t,buffer_limit*8> address_bytes{};
            std::array<uint8_t,buffer_limit*4> size_bytes{};
            if (!memory_.read(addresses,address_bytes.data(),count*8) ||
                !memory_.read(sizes,size_bytes.data(),count*4)) return error(0x1000);
            Job job;
            for(size_t i=0;i<count;++i) {
                const uint64_t address=ps4::get_le(address_bytes.data()+8*i,8);
                const uint64_t bytes=ps4::get_le(size_bytes.data()+4*i,4);
                if (!address || (address&3) || !bytes || (bytes&3) || bytes/4>word_limit-job.words.size()) return error(0x1000);
                std::vector<uint8_t> wire(size_t(bytes),0);
                if (!memory_.read(address,wire.data(),wire.size())) return error(0x1000);
                std::vector<uint32_t> words(wire.size()/4);
                for(size_t n=0;n<words.size();++n) words[n]=uint32_t(ps4::get_le(wire.data()+4*n,4));
                // Packet boundaries cannot straddle two separately submitted DCBs.
                if (!gpu::decodePM4Type3(words.data(),words.size())) return error(0x3013);
                job.words.insert(job.words.end(),words.begin(),words.end());
            }
            const auto plan=gpu::planPM4Triangles(job.words.data(),job.words.size());
            if (plan.error!=gpu::PM4MetalError::none) return error(0x3fff);
            job.info={next_,uint32_t(job.words.size()),uint32_t(plan.drawCount)};
            jobs_.push_back(std::move(job)); ++next_; return 0;
        } catch(const std::bad_alloc&) { return error(0x1001); }
    }
    int64_t done() {
        std::lock_guard<std::mutex> lock(lock_);
        if (!enabled_ || failed_) return error(0x10ff);
        sealed_=!jobs_.empty(); return 0;
    }
    int32_t snapshot(maxps4_gnm_submission& info, uint32_t* words, size_t capacity) {
        info={}; std::lock_guard<std::mutex> lock(lock_);
        if (!words && capacity) return -EINVAL;
        if (jobs_.empty()) return -EAGAIN;
        const auto& job=jobs_.front();
        if (words) {
            if (capacity<job.words.size()) return -EINVAL;
            std::copy(job.words.begin(),job.words.end(),words);
        }
        info=job.info; return 0;
    }
    int32_t complete(uint64_t ticket, bool success) {
        std::lock_guard<std::mutex> lock(lock_);
        if (!ticket || jobs_.empty() || jobs_.front().info.ticket!=ticket) return -EINVAL;
        jobs_.pop_front();
        if (!success) { clear(); failed_=true; }
        if (jobs_.empty()) sealed_=false;
        return 0;
    }
    void reset() { std::lock_guard<std::mutex> lock(lock_); clear(); enabled_=false; }
private:
    struct Job { maxps4_gnm_submission info{}; std::vector<uint32_t> words; };
    void clear() { jobs_.clear(); sealed_=failed_=false; }
    PS4GuestMemory& memory_;
    std::mutex lock_;
    std::deque<Job> jobs_;
    uint64_t next_=1;
    bool enabled_=false, sealed_=false, failed_=false;
};
}
