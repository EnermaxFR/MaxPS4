// SPDX-License-Identifier: GPL-2.0-only
// Sparse plaintext CNT/PFS, original synthetic bytes. No crypto or game data.
#include "PS4PFSFixture.hpp"
#include "../Native/PS4PKGBridge.h"
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <limits>
#include <unistd.h>
#include <sys/stat.h>

static void writeAt(int fd, uint64_t at, const uint8_t* bytes, size_t count) {
    assert(at <= uint64_t(std::numeric_limits<off_t>::max()));
    while (count) {
        const auto done = pwrite(fd, bytes, count, off_t(at));
        if (done < 0 && errno == EINTR) continue;
        assert(done > 0);
        at += uint64_t(done); bytes += done; count -= size_t(done);
    }
}
int main() {
    constexpr uint64_t bs = 65536;
    for (const auto base : {uint64_t(0x80010000), uint64_t(0x100010000), bs}) {
        // Last case tests an inode extent inside PFS beyond 4 GiB, independently
        // of a large CNT slice offset. Only five small blocks are ever written.
        const uint64_t payload = base == bs ? 0x100020000ULL : 4 * bs;
        const uint64_t pfsSize = payload + bs;
        auto pfs = pfsFixture::image();
        pfsFixture::le(pfs, 0x38, pfsSize / bs, 8);
        pfsFixture::le(pfs, bs + 2 * 0xa8 + 0x64, payload / bs, 4);
        auto header = pfsFixture::package(pfs);
        header.resize(bs);
        pfsFixture::be(header, 0x410, base, 8);
        pfsFixture::be(header, 0x418, pfsSize, 8);
        pfsFixture::be(header, 0x430, base + pfsSize, 8);
        char name[] = "/tmp/maxps4-pfs-v2-XXXXXX";
        const int fd = mkstemp(name); assert(fd >= 0);
        assert(ftruncate(fd, off_t(base + pfsSize)) == 0);
        writeAt(fd, 0, header.data(), header.size());
        writeAt(fd, base, pfs.data(), 4 * bs);
        writeAt(fd, base + payload, pfs.data() + 4 * bs, 5);
        size_t length = 0;
        assert(maxps4_pkg_pfs_read_file(name, "eboot.bin", nullptr, 0, &length) == -ENOSPC && length == 5);
        std::array<uint8_t, 8> output{}; output.fill(0xa5);
        assert(maxps4_pkg_pfs_read_file(name, "eboot.bin", output.data(), 4, &length) == -ENOSPC);
        for (auto c : output) assert(c == 0xa5);
        assert(maxps4_pkg_pfs_read_file(name, "eboot.bin", output.data(), output.size(), &length) == 0);
        assert(length == 5 && !std::memcmp(output.data(), "hello", 5) && output[5] == 0xa5);
        // Device-observed 0xd must remain header-only and inaccessible.
        pfs[0x1c] = 0xd; writeAt(fd, base, pfs.data(), 72);
        output.fill(0xa5); length = 99;
        assert(maxps4_pkg_pfs_read_file(name, "eboot.bin", output.data(), output.size(), &length) == -EACCES && length == 0);
        for (auto c : output) assert(c == 0xa5);
        struct stat st{}; assert(fstat(fd, &st) == 0);
        std::printf("PASS plaintext extraction CNT base=%llu PFS payload=%llu logical=%llu allocated=%llu; encrypted 0xd refused\n",
            (unsigned long long)base, (unsigned long long)payload,
            (unsigned long long)st.st_size, (unsigned long long)st.st_blocks * 512);
        assert(close(fd) == 0); assert(unlink(name) == 0);
    }
}
