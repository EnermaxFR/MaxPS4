#include "../Native/OrbisV4PRXLaunch.hpp"
#include "OrbisV4PRXFixture.h"
#include <cassert>
#include <iostream>
int main() {
    auto bytes=v4_prx_fixture::make();
    maxps4_v4_prx_request request{bytes.data(),bytes.size(),0x40000,0x44000,MAXPS4_V4_ABI_SYNTHETIC_HLT,3};
    std::unique_ptr<maxps4::OrbisV4Translation> owner;
    auto prepare=[&] { return maxps4::OrbisV4PRXLaunch::prepare(request,[] {},owner); };
    assert(prepare()==0 && owner);
    {
        auto lease=owner->lease();
        for (const auto& r:lease.ranges()) assert(r.guest!=r.host);
        assert(lease.memory().translate(maxps4::OrbisV4PRXLaunch::stack,owner->page_size(),6));
        assert(!lease.memory().translate(maxps4::OrbisV4PRXLaunch::stack,1,1));
        assert(!lease.memory().translate(0x44000,1,2));
    }
    owner->close(); owner.reset();
    request.instruction_budget=2; assert(prepare()==-ENOTSUP && !owner);
    request.instruction_budget=0; assert(prepare()==-EINVAL && !owner);
    request.instruction_budget=3;
    request.abi=0; assert(prepare()==-ENOTSUP && !owner); request.abi=MAXPS4_V4_ABI_SYNTHETIC_HLT;
    request.entry=0x48000; assert(prepare()==-ENOTSUP && !owner);
    request.entry=UINT64_MAX; assert(prepare()==-ENOTSUP && !owner); request.entry=0x44000;
    bytes[0x400]=0xeb; assert(prepare()==-ENOTSUP && !owner); bytes[0x400]=0x48;
    // Same bytes/count, but a pointer to an inaccessible gap or an NX fetch.
    bytes[0x402]=0; bytes[0x403]=0x50;
    assert(prepare()==-ENOTSUP && !owner);
    bytes=v4_prx_fixture::make(); request.file=bytes.data();
    orbis_test::put(bytes,7,68,4); assert(prepare()==-ENOTSUP && !owner); // W+X
    bytes=v4_prx_fixture::make(); request.file=bytes.data();
    // DT_INIT is otherwise parseable/preparable: startup must still be refused.
    size_t end=0x800;
    while (bytes[end] || bytes[end+1] || bytes[end+2] || bytes[end+3]) end+=16;
    orbis_test::put(bytes,12,end); orbis_test::put(bytes,0x4000,end+8);
    orbis_test::put(bytes,end-0x800+32,64+2*56+32);
    orbis_test::put(bytes,end-0x800+32,64+2*56+40);
    maxps4::OrbisPRXPreparation withInit;
    assert(withInit.prepare({{"init.prx",bytes,0x40000}}));
    assert(withInit.modules.front().metadata.init==0x4000);
    assert(prepare()==-ENOTSUP && !owner);
    bytes=v4_prx_fixture::make(); request.file=bytes.data();
    // A valid PT_TLS declaration remains unsupported, even for the same code.
    orbis_test::put(bytes,5,56,2);
    const size_t tls=64+4*56;
    orbis_test::put(bytes,7,tls,4); orbis_test::put(bytes,4,tls+4,4);
    orbis_test::put(bytes,0x700,tls+8); orbis_test::put(bytes,0x8100,tls+16);
    orbis_test::put(bytes,4,tls+32); orbis_test::put(bytes,16,tls+40); orbis_test::put(bytes,1,tls+48);
    maxps4::OrbisMetadata tlsMetadata;
    assert(tlsMetadata.parse(bytes.data(),bytes.size()));
    assert(prepare()==-ENOTSUP && !owner);
    bytes=v4_prx_fixture::make(99); request.file=bytes.data();
    assert(prepare()==0 && owner);
    uint32_t value=0;
    assert(owner->read(0x48010,&value,4)==0 && value==99);
    owner->close(); owner.reset();
    std::cout << "PASS V4 PRX admission: explicit synthetic ABI, bounded HLT, readable guest operand, RX/RW-NX stack, unsupported code/ABI/init and failure cleanup; no CPU execution\n";
}
