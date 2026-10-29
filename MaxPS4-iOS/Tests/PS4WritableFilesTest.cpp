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
    // sceSaveDataMount2 mounts <save root>/<dirName> at /savedata0, keeping
    // the game's other descriptors open.
    char saves[] = "/tmp/maxps4-saves-XXXXXX";
    assert(::mkdtemp(saves));
    const std::string game = std::string(saves) + "/game";
    { const int host = ::open(game.c_str(), O_CREAT | O_WRONLY, 0644); assert(host >= 0); ::close(host); }
    assert(kernel.mount_app0(saves) == 0);
    const auto app = call(S::open, path("/app0/game"), 0);
    assert(app >= 0);
    auto put = [&](uint64_t address, uint64_t value, size_t width) {
        uint8_t wire[8]; put_le(wire, 0, value, width); assert(memory.write(address, wire, width));
    };
    auto mount = [&](uint32_t mode) {
        uint8_t zero[96]{}; assert(memory.write(0x11400, zero, sizeof(zero)));
        put(0x11400, 1, 4); put(0x11408, path("SAVEDATA00", 0x11500), 8); put(0x11418, mode, 4);
        return call(S::save_mount2, 0x11400, 0x11600);
    };
    const int64_t not_found = signed32(0x809f0008U);
    assert(call(S::save_init) == 0 && mount(0x22) == not_found); // No save root yet.
    assert(kernel.set_savedata_root(saves) == 0);
    assert(mount(0x2) == not_found);
    assert(mount(0x22) == 0); // RDWR | CREATE2: created.
    char point[16]{};
    assert(memory.read(0x11600, point, sizeof(point)) && !std::strcmp(point, "/savedata0"));
    uint8_t status[4]{}; assert(memory.read(0x11600 + 28, status, 4) && get_le(status, 4) == 1);
    assert(mount(0x22) == signed32(0x809f0003U)); // Already mounted.
    const auto file = call(S::open, path("/savedata0/data"), 0x202);
    assert(file >= 0 && call(S::close, file) == 0);
    assert(::access((std::string(saves) + "/SAVEDATA00/data").c_str(), F_OK) == 0);
    assert(call(S::save_umount, path("/savedata0", 0x11700)) == 0);
    assert(call(S::save_umount, 0x11700) == signed32(0x809f0004U));
    assert(call(S::open, path("/savedata0/data")) == kernel_error(noent));
    assert(mount(0x2) == 0 && memory.read(0x11600 + 28, status, 4) && get_le(status, 4) == 0);
    assert(mount(0x4) != 0 && call(S::save_term) == 0 && mount(0x4) == signed32(0x809f0007U));
    assert(call(S::close, app) == 0); // Untouched by the save mounts.
    assert(call(S::save_dialog_init) == 0 && call(S::save_dialog_update) == 1);
    assert(call(S::save_dialog_result, 0x11800) == signed32(0x80b80008U));
    assert(call(S::save_dialog_open, 0x11800) == 0 && call(S::save_dialog_update) == 3);
    assert(call(S::save_dialog_result, 0x11800) == 0 && memory.read(0x11808, status, 4) && get_le(status, 4) == 1);
    assert(call(S::save_dialog_term) == 0);
    assert(resolve_libkernel("0z45PIH+SNI", "libSceSaveData", "libSceSaveData", 1) == S::save_mount2);
    kernel.reset();
    assert(::unlink((std::string(saves) + "/SAVEDATA00/data").c_str()) == 0);
    assert(::unlink(game.c_str()) == 0);
    assert(::rmdir((std::string(saves) + "/SAVEDATA00").c_str()) == 0 && ::rmdir(saves) == 0);
    // A game update over /app0: its files first, the game's for the rest.
    char base_dir[] = "/tmp/maxps4-base-XXXXXX", update_dir[] = "/tmp/maxps4-update-XXXXXX";
    assert(::mkdtemp(base_dir) && ::mkdtemp(update_dir));
    auto host_file = [](const std::string& name, const char* text) {
        const int host = ::open(name.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        assert(host >= 0 && ::write(host, text, std::strlen(text)) == ssize_t(std::strlen(text))); ::close(host);
    };
    host_file(std::string(base_dir) + "/data", "base"); host_file(std::string(base_dir) + "/only", "game");
    host_file(std::string(update_dir) + "/data", "upd!");
    assert(kernel.mount_app0_update(update_dir) == kernel_error(inval)); // No /app0 yet (reset above).
    assert(kernel.mount_app0(base_dir) == 0 && kernel.mount_app0_update(update_dir) == 0);
    auto app0 = [&](const char* name) {
        const auto fd = call(S::open, path(name), 0);
        assert(fd >= 0);
        char text[8]{};
        assert(call(S::read, fd, 0x11000, 4) == 4 && memory.read(0x11000, text, 4) && call(S::close, fd) == 0);
        return std::string(text);
    };
    assert(app0("/app0/data") == "upd!" && app0("/app0/only") == "game");
    assert(call(S::open, path("/app0/data"), 0x202) == kernel_error(rofs));
    assert(kernel.mount_app0(base_dir) == 0 && app0("/app0/data") == "base"); // A remount drops the update.
    kernel.reset();
    for (const auto& name : {std::string(base_dir) + "/data", std::string(base_dir) + "/only", std::string(update_dir) + "/data"})
        assert(::unlink(name.c_str()) == 0);
    assert(::rmdir(base_dir) == 0 && ::rmdir(update_dir) == 0);
    // System services games query at start: language, clock, PSN, content.
    auto value32 = [&](uint64_t address) { uint8_t w[4]{}; assert(memory.read(address, w, 4)); return get_le(w, 4); };
    kernel.set_system_language(2);
    assert(call(S::system_param_int, 1, 0x11900) == 0 && value32(0x11900) == 2);
    uint8_t date[16]{}; // 2026-10-29 12:34:56.000789
    put_le(date, 0, 2026, 2); put_le(date, 2, 10, 2); put_le(date, 4, 29, 2);
    put_le(date, 6, 12, 2); put_le(date, 8, 34, 2); put_le(date, 10, 56, 2); put_le(date, 12, 789, 4);
    assert(memory.write(0x11a00, date, sizeof(date)));
    assert(call(S::rtc_get_tick, 0x11a00, 0x11a20) == 0);
    uint8_t tick[8]{}; assert(memory.read(0x11a20, tick, 8));
    assert(get_le(tick, 8) == 62135596800000000ULL + (20755ULL * 86400 + 12 * 3600 + 34 * 60 + 56) * 1000000 + 789);
    assert(call(S::rtc_set_tick, 0x11a40, 0x11a20) == 0);
    uint8_t back[16]{}; assert(memory.read(0x11a40, back, 16) && !std::memcmp(back, date, 16));
    put_le(date, 2, 13, 2); assert(memory.write(0x11a00, date, sizeof(date)));
    assert(call(S::rtc_get_tick, 0x11a00, 0x11a20) == signed32(0x80b50002U)); // Month 13.
    assert(call(S::rtc_current_tick, 0x11a20) == 0 && call(S::rtc_resolution) == 1000000);
    assert(call(S::np_state, 1, 0x11900) == 0 && value32(0x11900) == 1);
    assert(call(S::np_signed_out, 1, 0x11900) == signed32(0x80550006U));
    assert(call(S::app_param_int, 0, 0x11900) == 0 && value32(0x11900) == 3);
    assert(call(S::app_addcont_list, 0, 0, 0, 0x11900) == 0 && value32(0x11900) == 0);
    // Add-on content: listed as installed, found by its label, mounted at /addcont0.
    char dlc_dir[] = "/tmp/maxps4-dlc-XXXXXX";
    assert(::mkdtemp(dlc_dir));
    host_file(std::string(dlc_dir) + "/extra", "dlc!");
    assert(call(S::app_addcont_list, 0, 0, 0, 0x11900) == 0 && value32(0x11900) == 0);
    assert(kernel.add_addcont("SHORT", dlc_dir) == kernel_error(inval));
    assert(kernel.add_addcont("SONICMANIAPLUS00", dlc_dir) == 0);
    assert(call(S::app_addcont_list, 0, 0, 0, 0x11900) == 0 && value32(0x11900) == 1);
    assert(call(S::app_addcont_list, 0, 0x11b00, 4, 0x11900) == 0 && value32(0x11900) == 1);
    char label[17]{}; assert(memory.read(0x11b00, label, 16) && !std::strcmp(label, "SONICMANIAPLUS00"));
    assert(value32(0x11b00 + 20) == 4); // Installed.
    assert(memory.write(0x11c00, "SONICMANIAPLUS00", 17) && memory.write(0x11c20, "SOMETHINGELSE000", 17));
    assert(call(S::app_addcont_info, 0, 0x11c20, 0x11d00) == signed32(0x80d90005U));
    assert(call(S::app_addcont_info, 0, 0x11c00, 0x11d00) == 0 && value32(0x11d00 + 20) == 4);
    assert(call(S::app_addcont_mount, 0, 0x11c00, 0x11d40) == 0);
    char where[16]{}; assert(memory.read(0x11d40, where, 16) && !std::strcmp(where, "/addcont0"));
    const auto extra = call(S::open, path("/addcont0/extra"), 0);
    assert(extra >= 0 && call(S::read, extra, 0x11000, 4) == 4 && call(S::close, extra) == 0);
    char dlc_text[5]{}; assert(memory.read(0x11000, dlc_text, 4) && !std::strcmp(dlc_text, "dlc!"));
    assert(call(S::app_addcont_unmount, 0x11d40) == 0 && call(S::app_addcont_unmount, 0x11d40) == signed32(0x80d90002U));
    assert(call(S::open, path("/addcont0/extra"), 0) == kernel_error(noent));
    assert(::unlink((std::string(dlc_dir) + "/extra").c_str()) == 0 && ::rmdir(dlc_dir) == 0);
    assert(resolve_libkernel("18B2NS1y9UU", "libSceRtc", "libSceRtc", 1) == S::rtc_current_tick);
    // POSIX pthread_* reuse the scePthread services and return errno numbers.
    assert(resolve_libkernel("7H0iTOciTLo", "libkernel", "libkernel", 1) == S::mutex_lock);
    assert(resolve_libkernel("OxhIB8LB-PQ", "libkernel", "libkernel", 1) == S::thread_create);
    assert(pthread_result(0) == 0 && pthread_result(kernel_error(busy)) == 16 && pthread_result(kernel_error(timedout)) == 60);
    assert(pthread_result(42) == 42);
    kernel.reset();
    assert(call(S::system_param_int, 1, 0x11900) == 0 && value32(0x11900) == 1);
    std::puts("PS4 writable files, positioned I/O and directory checks passed");
}
