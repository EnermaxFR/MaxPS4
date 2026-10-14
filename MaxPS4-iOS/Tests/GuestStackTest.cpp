#include "../Native/maxps4_guest_stack.h"
#include <array>
#include <cassert>
#include <cstdio>
int main() {
    alignas(16) std::array<uint8_t, 4096> stack{};
    for (size_t offset = 0; offset < 16; ++offset) {
        stack.fill(0xa5);
        uint64_t rsp = 99;
        assert(maxps4::initialize_guest_stack(stack.data()+offset,stack.size()-offset,0x123456,4096,rsp)==0);
        assert(!(rsp & 15) && rsp >= reinterpret_cast<uintptr_t>(stack.data()+offset));
        assert(rsp + 72 <= reinterpret_cast<uintptr_t>(stack.data()+stack.size()));
        uint64_t words[9]{};
        std::memcpy(words,reinterpret_cast<void*>(rsp),sizeof(words));
        assert(words[0]==0 && words[1]==0 && words[2]==0);
        assert(words[3]==6 && words[4]==4096 && words[5]==9 && words[6]==0x123456);
        assert(words[7]==0 && words[8]==0);
        assert(stack[0]==0xa5); // Initialization stays within the reserved stack.
    }
    uint64_t rsp=99;
    assert(maxps4::initialize_guest_stack(nullptr,4096,1,4096,rsp)==-EINVAL && !rsp);
    assert(maxps4::initialize_guest_stack(stack.data(),80,1,4096,rsp)==-EINVAL && !rsp);
    assert(maxps4::initialize_guest_stack(stack.data(),4096,0,4096,rsp)==-EINVAL && !rsp);
    assert(maxps4::initialize_guest_stack(stack.data(),4096,1,3,rsp)==-EINVAL && !rsp);
    std::puts("PASS minimal x86-64 process stack: alignment, argc/argv/envp, auxv, bounds and invalid inputs");
}
