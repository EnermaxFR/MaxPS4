#include "../Native/maxps4_ps4_gnm_queue.h"
#include "../Native/maxps4_ps4_gnm.h"
#include "GuestELFFixture.h"
#include <cassert>
#include <cstdio>
#include <thread>
using namespace maxps4;
int main() {
    GuestELF image; auto file=guest_test::elf(40,2,true);
    guest_test::put(file,4096,160,8); assert(image.load(file.data(),file.size(),6)==0);
    PS4GuestMemory memory(image); PS4Gnm writer(memory); PS4GnmQueue queue(memory), other(memory);
    constexpr uint64_t base=0x10000, addresses=base+0x100, sizes=base+0x180;
    auto put=[&](uint64_t at,uint64_t value,size_t bytes) {
        uint8_t wire[8]{}; ps4::put_le(wire,0,value,bytes); assert(memory.write(at,wire,bytes));
    };
    auto submit=[&] { return queue.submit(1,addresses,sizes,0,0); };
    put(addresses,base,8); put(sizes,28,4); assert(writer.draw_auto(base,7,3,0)==0);
    assert(!queue.allowed() && submit()==PS4GnmQueue::error(0x10ff));
    assert(queue.done()==PS4GnmQueue::error(0x10ff));
    assert(queue.backend(true)==0 && queue.allowed()==1 && !other.allowed());
    assert(queue.submit(0,addresses,sizes,0,0)==PS4GnmQueue::error(0x1000));
    assert(queue.submit(17,addresses,sizes,0,0)==PS4GnmQueue::error(0x1000));
    assert(queue.submit(1,UINT64_MAX,sizes,0,0)==PS4GnmQueue::error(0x1000));
    assert(queue.submit(1,addresses,sizes,addresses,sizes)==PS4GnmQueue::error(0x3fff));
    for(auto size:{0U,3U,65536U*4+4}) {
        put(sizes,size,4); assert(submit()==PS4GnmQueue::error(0x1000));
    }
    put(sizes,28,4);
    put(base,0xc03f2d00,4); assert(submit()==PS4GnmQueue::error(0x3013));
    assert(writer.draw_auto(base,7,3,0)==0);
    assert(writer.vgt(base+0x40,3,63,0,0)==0);
    put(addresses,base+0x40,8); put(sizes,12,4); assert(submit()==PS4GnmQueue::error(0x3fff));
    put(addresses,base,8); put(sizes,28,4); assert(submit()==0);
    maxps4_gnm_submission info{}; assert(queue.snapshot(info,nullptr,0)==0 && info.word_count==7 && info.draw_count==1);
    const uint64_t first=info.ticket;
    uint32_t words[8]{}; words[0]=0x12345678;
    assert(queue.snapshot(info,words,6)==-EINVAL && info.ticket==0 && words[0]==0x12345678);
    put(base+4,99,4); assert(queue.snapshot(info,words,8)==0 && words[1]==3); // owned immutable copy
    assert(queue.complete(first+1,true)==-EINVAL && queue.allowed()==1);
    assert(writer.draw_auto(base,7,3,0)==0);
    for(int i=0;i<3;++i) assert(submit()==0);
    assert(!queue.allowed() && submit()==PS4GnmQueue::error(0x1001));
    assert(queue.done()==0 && !queue.allowed());
    assert(queue.complete(first,true)==0 && !queue.allowed()); // sealed until ALL queued work is done
    assert(queue.complete(first,true)==-EINVAL);
    for(int i=0;i<3;++i) { assert(queue.snapshot(info,words,8)==0); assert(queue.complete(info.ticket,true)==0); }
    assert(queue.allowed()==1 && queue.snapshot(info,nullptr,0)==-EAGAIN);
    // Two DCBs, one submission. Validate all before publication.
    put(addresses+8,base,8); put(sizes+4,28,4);
    put(addresses+8,UINT64_MAX-3,8);
    assert(queue.submit(2,addresses,sizes,0,0)==PS4GnmQueue::error(0x1000));
    assert(queue.snapshot(info,nullptr,0)==-EAGAIN);
    put(addresses+8,base,8);
    assert(queue.submit(2,addresses,sizes,0,0)==0);
    assert(queue.snapshot(info,nullptr,0)==0 && info.draw_count==2 && info.word_count==14);
    const auto old=info.ticket; assert(queue.backend(false)==0 && queue.complete(old,true)==-EINVAL);
    assert(queue.backend(true)==0 && submit()==0);
    assert(queue.snapshot(info,nullptr,0)==0 && info.ticket>old);
    assert(submit()==0);
    assert(queue.complete(info.ticket,false)==0 && !queue.allowed());
    assert(queue.snapshot(info,nullptr,0)==-EAGAIN);
    assert(submit()==PS4GnmQueue::error(0x10ff));
    assert(queue.backend(true)==0 && submit()==0);
    assert(queue.snapshot(info,nullptr,0)==0); const auto beforeReset=info.ticket;
    queue.reset(); assert(!queue.allowed() && queue.complete(beforeReset,true)==-EINVAL);
    assert(queue.backend(true)==0);
    // Host snapshots/completions can progress independently of guest dispatch.
    std::thread consumer([&] {
        for(int i=0;i<100;++i) {
            maxps4_gnm_submission next{}; uint32_t copied[7]{};
            while(queue.snapshot(next,copied,7)==-EAGAIN) std::this_thread::yield();
            assert(next.ticket>beforeReset && copied[1]==3);
            assert(queue.complete(next.ticket,true)==0);
        }
    });
    for(int i=0;i<100;++i) {
        int64_t result;
        do { result=submit(); if(result) { assert(result==PS4GnmQueue::error(0x1001)); std::this_thread::yield(); } } while(result);
    }
    consumer.join(); assert(queue.snapshot(info,nullptr,0)==-EAGAIN);
    std::puts("PASS GNM bounded owned queue: validation, immutable multi-DCB copies, FIFO, sealing, failure, reset and concurrency");
}
