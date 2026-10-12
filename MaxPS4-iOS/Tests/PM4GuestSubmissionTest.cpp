#include "../Native/PM4GuestSubmission.hpp"
#include "../Native/maxps4_guest_elf.h"
#include "GuestELFFixture.h"
#include <cassert>
#include <cstdio>
int main() {
    using namespace maxps4::gpu;
    auto file = guest_test::elf(40, 2, true);
    file.resize(207);
    guest_test::put(file, 12, 152, 8);
    guest_test::put(file, 0xc0012d00, 195, 4);
    guest_test::put(file, 3, 199, 4);
    guest_test::put(file, 2, 203, 4);
    maxps4::GuestELF image;
    assert(image.load(file.data(), file.size(), 3) == 0);
    auto out = snapshotPM4(image, 0x10000, 3);
    assert(out.error == 0 && out.words == std::vector<uint32_t>({0xc0012d00, 3, 2}));
    assert(snapshotPM4(image, 0x10001, 3).error == -EINVAL);
    assert(snapshotPM4(image, UINT64_MAX - 3, 3).error == -EINVAL);
    assert(snapshotPM4(image, 0x10000, kMaxSubmissionWords + 1).error == -EINVAL);
    assert(snapshotPM4(image, 0x10000, 0).error == -EINVAL);
    assert(snapshotPM4(image, 0x1003c, 3).error == -EFAULT);
    assert(snapshotPM4(image, 0xffff, 3).error == -EINVAL);
    assert(snapshotPM4(image, 0, 3).error == -EFAULT);
    assert(snapshotPM4(image, 0x1000, 3).error == -ENOTSUP); // x86 is not PM4.
    // Snapshot survives source mutation and destruction; no stale arena pointer.
    image.clear();
    assert(out.words[2] == 2);
    assert(snapshotPM4(image, 0x10000, 3).error == -EFAULT);
    guest_test::put(file, 0xc001ff00, 195, 4);
    assert(image.load(file.data(), file.size(), 3) == 0);
    out = snapshotPM4(image, 0x10000, 3);
    assert(out.error == -ENOTSUP && out.words.empty());
    std::puts("PASS actual ELF arena -> owned PM4 snapshot; bounds, alignment, lifetime and rejection");
}
