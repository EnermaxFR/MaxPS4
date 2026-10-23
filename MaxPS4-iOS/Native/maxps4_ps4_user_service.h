#pragma once
#include "maxps4_ps4_guest_memory.h"
#include "maxps4_ps4_abi.h"
#include <mutex>

namespace maxps4 {
// Engine-local virtual profile. This is not a PSN account, Sony identity,
// persisted console user or host OS user. All outputs use checked guest copies.
class PS4UserService final {
public:
    static constexpr uint32_t local_user = 1;
    static constexpr char local_name[] = "MaxPS4";
    static int64_t error(uint32_t code) { return ps4::signed32(0x80960000U | code); }
    explicit PS4UserService(PS4GuestMemory& memory) : memory_(memory) {}
    int64_t initialize(uint64_t parameters) {
        std::lock_guard<std::mutex> lock(guard_);
        if (initialized_) return error(3);
        if (parameters) {
            uint8_t wire[4];
            if (!memory_.read(parameters, wire, sizeof(wire))) return error(5);
            const uint64_t priority = ps4::get_le(wire, 4);
            if (priority < 0x100 || priority > 0x2ff) return error(5);
        }
        initialized_ = true; login_pending_ = true; return 0;
    }
    int64_t terminate() {
        std::lock_guard<std::mutex> lock(guard_);
        if (!initialized_) return error(2);
        initialized_ = login_pending_ = false; return 0;
    }
    int64_t initial_user(uint64_t output) {
        std::lock_guard<std::mutex> lock(guard_);
        if (!initialized_) return error(2);
        uint8_t wire[4]; ps4::put_le(wire, 0, local_user, 4);
        return output && memory_.write(output, wire, sizeof(wire)) ? 0 : error(5);
    }
    int64_t login_users(uint64_t output) {
        std::lock_guard<std::mutex> lock(guard_);
        if (!initialized_) return error(2);
        uint8_t wire[16];
        for (size_t i = 0; i < 4; ++i) ps4::put_le(wire, i * 4, i ? UINT32_MAX : local_user, 4);
        return output && memory_.write(output, wire, sizeof(wire)) ? 0 : error(5);
    }
    int64_t event(uint64_t output) {
        std::lock_guard<std::mutex> lock(guard_);
        if (!initialized_) return error(2);
        if (!output || !memory_.permitted(output, 8, 2) || memory_.permitted(output, 8, 1)) return error(5);
        if (!login_pending_) return error(7);
        uint8_t wire[8]{}; // LOGIN=0, user ID at byte 4.
        ps4::put_le(wire, 4, local_user, 4);
        if (!memory_.write(output, wire, sizeof(wire))) return error(5);
        login_pending_ = false; return 0;
    }
    int64_t user_name(int32_t user, uint64_t output, uint64_t size) {
        std::lock_guard<std::mutex> lock(guard_);
        if (!initialized_) return error(2);
        if (user == -1 || !output) return error(5);
        if (user != int32_t(local_user)) return error(9);
        if (size < sizeof(local_name)) return error(10);
        return memory_.write(output, local_name, sizeof(local_name)) ? 0 : error(5);
    }
    void reset() { std::lock_guard<std::mutex> lock(guard_); initialized_ = login_pending_ = false; }
private:
    PS4GuestMemory& memory_;
    std::mutex guard_;
    bool initialized_ = false, login_pending_ = false;
};
}
