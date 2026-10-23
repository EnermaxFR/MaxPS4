#include "../Native/maxps4_ps4_libkernel.h"
#include "GuestELFFixture.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace maxps4;
using namespace maxps4::ps4;
using S = PS4KernelService;

int main() {
    GuestELF image;
    auto elf = guest_test::elf(40, 2, true);
    guest_test::put(elf, 8192, 160, 8);
    assert(image.load(elf.data(), elf.size(), 6) == 0);
    PS4GuestMemory memory(image);
    PS4Libkernel kernel(memory);
    auto path = [&](const char* value, uint64_t address = 0x10000) {
        assert(memory.write(address, value, std::strlen(value) + 1));
        return address;
    };
    auto call = [&](S service, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0, uint64_t d = 0) {
        return kernel.invoke(service, {a, b, c, d});
    };
    char root[] = "/tmp/maxps4-writable-XXXXXX";
    assert(::mkdtemp(root));
    const std::string base(root);
    assert(call(S::open, path("/savedata0/save"), 0x202) == kernel_error(noent));
    assert(kernel.mount("/unknown", root) == kernel_error(inval));
    assert(kernel.mount(nullptr, root) == kernel_error(inval));
    assert(kernel.mount("/savedata0", root) == 0);
    assert(kernel.mount("/download0", root) == 0);
    assert(kernel.mount("/temp0", root) == 0);
    assert(kernel.mount_app0(root) == 0);
    assert(call(S::open, path("/app0/save"), 0x202) == kernel_error(rofs));
    for (const char* invalid : {"/savedata0/../save", "/savedata0//save", "/savedata0/./save", "/savedata0/save/"})
        assert(call(S::open, path(invalid), 0x202) == kernel_error(inval));
    assert(call(S::open, path("/savedata0/save"), 0x206) == kernel_error(notsup));
    const auto fd = call(S::open, path("/savedata0/save"), 0xa02); // RDWR | CREAT | EXCL.
    assert(fd == 3);
    assert(call(S::open, 0x10000, 0xa02) == kernel_error(exist));
    assert(memory.write(0x11000, "abcdef", 6));
    assert(call(S::write, fd, 0x11000, 6) == 6);
    assert(call(S::write, fd, 0x11fff, 4) == kernel_error(fault));
    assert(call(S::write, fd, 0x11000, UINT64_MAX) == kernel_error(inval));
    assert(call(S::lseek, fd, 0, 1) == 6);
    assert(call(S::pread, fd, 0x11100, 3, 1) == 3);
    char bytes[8]{};
    assert(memory.read(0x11100, bytes, 3) && std::memcmp(bytes, "bcd", 3) == 0);
    assert(call(S::pwrite, fd, 0x11000, 2, 2) == 2);
    assert(call(S::lseek, fd, 0, 1) == 6); // Positioned I/O preserves the offset.
    assert(call(S::pread, fd, 0x11100, 6, 0) == 6);
    assert(memory.read(0x11100, bytes, 6) && std::memcmp(bytes, "ababef", 6) == 0);
    assert(call(S::pread, fd, 0x11100, 1, UINT64_MAX) == kernel_error(inval));
    assert(call(S::pwrite, fd, 0x11000, 1, UINT64_MAX) == kernel_error(inval));
    assert(call(S::pread, fd, 0x11fff, 4, 0) == kernel_error(fault));
    assert(call(S::fsync, fd) == 0);
    assert(call(S::ftruncate, fd, UINT64_MAX) == kernel_error(inval));
    assert(call(S::ftruncate, fd, 3) == 0);
    assert(call(S::close, fd) == 0);
    assert(call(S::write, fd, 0x11000, 1) == kernel_error(badf));
    assert(call(S::fsync, fd) == kernel_error(badf));
    const auto readonly = call(S::open, path("/savedata0/save"));
    assert(readonly == 3);
    assert(call(S::write, readonly, 0x11000, 1) == kernel_error(badf));
    assert(call(S::ftruncate, readonly, 0) == kernel_error(badf));
    assert(call(S::fstat, readonly, 0x11200) == 0);
    uint8_t stat[PS4Libkernel::stat_size];
    assert(memory.read(0x11200, stat, sizeof(stat)));
    assert(get_le(stat + 72, 8) == 3 && (get_le(stat + 8, 2) & 0200));
    assert(call(S::close, readonly) == 0);
    const auto append = call(S::open, 0x10000, 9); // WRONLY | APPEND.
    assert(append == 3 && call(S::lseek, append, 0) == 0);
    assert(call(S::write, append, 0x11000, 2) == 2);
    assert(call(S::lseek, append, 0, 2) == 5);
    assert(call(S::read, append, 0x11100, 1) == kernel_error(badf));
    assert(call(S::close, append) == 0);
    assert(call(S::truncate, path("/savedata0/save"), 2) == 0);
    assert(call(S::truncate, path("/app0/save"), 0) == kernel_error(rofs));
    assert(call(S::rename, path("/savedata0/save"), path("/download0/save", 0x10100)) == kernel_error(xdev));
    assert(call(S::mkdir, path("/savedata0/sub"), 0700) == 0);
    assert(call(S::mkdir, 0x10000, 0700) == kernel_error(exist));
    assert(call(S::rename, path("/savedata0/save"), path("/savedata0/sub/renamed", 0x10100)) == 0);
    assert(call(S::rmdir, path("/savedata0/sub")) == kernel_error(notempty));
    assert(call(S::unlink, path("/savedata0/sub/renamed")) == 0);
    assert(call(S::rmdir, path("/savedata0/sub")) == 0);
    assert(call(S::rmdir, path("/savedata0")) == kernel_error(busy));
    // A guest path cannot follow a link into another host directory.
    assert(::symlink("/tmp", (base + "/escape").c_str()) == 0);
    assert(call(S::open, path("/savedata0/escape")) == kernel_error(loop));
    assert(call(S::open, path("/savedata0/escape/outside"), 0x202) < 0);
    assert(call(S::mkdir, path("/savedata0/escape/outside"), 0700) < 0);
    assert(::unlink((base + "/escape").c_str()) == 0);
    // Force several dirent blocks and validate their wire layout and offsets.
    std::set<std::string> expected{".", ".."}, actual;
    for (unsigned i = 0; i < 60; ++i) {
        const std::string name = "entry-" + std::to_string(i);
        expected.insert(name);
        assert(::mkdir((base + "/" + name).c_str(), 0700) == 0);
    }
    const auto directory = call(S::open, path("/savedata0/"), 0x20000);
    assert(directory == 3);
    assert(call(S::getdirentries, directory, 0x11000, 511) == kernel_error(inval));
    assert(call(S::getdirentries, directory, 0x11000, UINT64_MAX) == kernel_error(inval));
    assert(call(S::getdirentries, directory, 0x11fff, 512) == kernel_error(fault));
    assert(call(S::getdirentries, directory, 0x11000, 512, 0x11fff) == kernel_error(fault));
    assert(call(S::lseek, directory, 0, 1) == 0);
    uint64_t position = 0;
    for (;;) {
        const auto count = call(S::getdirentries, directory, 0x11000, 512, 0x11300);
        assert(count == 0 || count == 512);
        uint8_t offset[8];
        assert(memory.read(0x11300, offset, 8) && get_le(offset, 8) == position);
        if (!count) break;
        uint8_t block[512];
        assert(memory.read(0x11000, block, sizeof(block)));
        size_t cursor = 0;
        while (cursor < sizeof(block)) {
            const auto size = get_le(block + cursor + 4, 2);
            const auto length = block[cursor + 7];
            assert(size >= 8U + length + 1U && size % 4 == 0 && cursor + size <= sizeof(block));
            assert(block[cursor + 6] == 4 && block[cursor + 8 + length] == 0);
            assert(actual.insert(std::string(reinterpret_cast<char*>(block + cursor + 8), length)).second);
            cursor += size;
        }
        position += 512;
    }
    assert(position > 512 && actual == expected);
    assert(call(S::lseek, directory, 1) == kernel_error(inval));
    assert(call(S::lseek, directory, uint64_t(INT64_MAX), 1) == kernel_error(overflow));
    assert(call(S::lseek, directory, 0) == 0);
    assert(call(S::getdirentries, directory, 0x11000, 512) == 512);
    assert(kernel.mount("/savedata0", "/path/that/does/not/exist") == kernel_error(noent));
    assert(call(S::lseek, directory, 0, 1) == 512);
    assert(kernel.mount("/savedata0", root) == 0);
    assert(call(S::close, directory) == kernel_error(badf));
    assert(resolve_libkernel("4wSze92BhLI", "libkernel", "libkernel", 1) == S::write);
    assert(resolve_libkernel("taRWhTJFTgE", "libkernel", "libkernel", 1) == S::getdirentries);
    assert(resolve_libkernel("write", "libkernel", "libkernel", 1) == S::write);
    assert(call(S::write, 1, UINT64_MAX, 0) == 0);
    assert(call(S::write, 2, UINT64_MAX, 0) == 0);
    kernel.reset();
    assert(call(S::open, path("/savedata0")) == kernel_error(noent));
    for (const auto& name : expected)
        if (name != "." && name != "..") assert(::rmdir((base + "/" + name).c_str()) == 0);
    assert(::rmdir(root) == 0);
    std::puts("PS4 writable files, positioned I/O and directory checks passed");
}
