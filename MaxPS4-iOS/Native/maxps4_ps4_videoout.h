#pragma once
#include "maxps4_ps4_threads.h"
#include "maxps4_ps4_videoout_abi.h"
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <mutex>
#include <new>
#include <vector>

namespace maxps4 {
// Bounded, CPU-visible linear VideoOut surface. Submission takes an owned
// snapshot; only a host presentation confirmation advances flip status/events.
// Lock order: VideoOut guard -> PS4Threads guard. No runtime lock in host callbacks.
class PS4VideoOut final {
public:
    static constexpr size_t attribute_size = 40, buffer_limit = 16, group_limit = 4;
    static constexpr uint32_t bgra = 0x80000000U, rgba = 0x80002200U;
    static int64_t error(uint32_t value) { return ps4::signed32(0x80290000U | value); }
    PS4VideoOut(PS4GuestMemory& memory, PS4Threads& events) : memory_(memory), events_(events) {}
    int64_t open(int32_t bus, int32_t index, uint64_t parameter) {
        std::lock_guard<std::mutex> lock(guard_);
        if (bus != 0 || index != 0 || parameter) return error(1);
        if (port_) return error(9);
        if (next_port_ > INT32_MAX) return error(0x17);
        port_ = next_port_++;
        return int64_t(port_);
    }
    int64_t close(uint64_t port) {
        std::lock_guard<std::mutex> lock(guard_);
        if (!valid(port)) return error(11);
        clear(); return 0;
    }
    void reset() { std::lock_guard<std::mutex> lock(guard_); clear(); }
    int64_t set_attribute(uint64_t output, uint32_t format, uint32_t tiling, uint32_t aspect,
                          uint32_t width, uint32_t height, uint32_t pitch) {
        uint8_t wire[attribute_size]{};
        const uint32_t values[] = {format, tiling, aspect, width, height, pitch};
        for (size_t i = 0; i < 6; ++i) ps4::put_le(wire, i * 4, values[i], 4);
        return memory_.write(output, wire, sizeof(wire)) ? 0 : error(2);
    }
    int64_t register_buffers(uint64_t port, int32_t start, uint64_t addresses,
                             int32_t count, uint64_t attribute) {
        std::lock_guard<std::mutex> lock(guard_);
        if (!valid(port)) return error(11);
        if (start < 0 || count < 1 || count > int32_t(buffer_limit) || start > int32_t(buffer_limit) - count) return error(10);
        uint8_t wire[attribute_size]{}, pointers[buffer_limit * 8]{};
        if (!memory_.read(attribute, wire, sizeof(wire)) || !memory_.read(addresses, pointers, size_t(count) * 8)) return error(2);
        Attribute attr{};
        attr.format = uint32_t(ps4::get_le(wire, 4));
        attr.width = uint32_t(ps4::get_le(wire + 12, 4)); attr.height = uint32_t(ps4::get_le(wire + 16, 4));
        attr.pitch = uint32_t(ps4::get_le(wire + 20, 4));
        if (attr.format != rgba && attr.format != bgra) return error(3);
        // Tiling 0 (tiled, what games use) or 1 (linear). Tiled frames are
        // marked as such, never presented as linear rows.
        const uint64_t tiling = ps4::get_le(wire + 4, 4);
        if (tiling > 1) return error(7);
        attr.tiled = tiling == 0;
        if (ps4::get_le(wire + 8, 4) != 0) return error(8);
        if (!attr.width || !attr.height || attr.width > 1920 || attr.height > 1080) return error(5);
        if (attr.pitch < attr.width || attr.pitch > 2048) return error(4);
        for (size_t i = 24; i < sizeof(wire); ++i) if (wire[i]) return error(0x1a);
        size_t group = 0;
        while (group < groups_.size() && groups_[group].used) ++group;
        if (group == groups_.size()) return error(15);
        for (int32_t i = 0; i < count; ++i) {
            if (buffers_[size_t(start + i)].used) return error(16);
            const uint64_t address = ps4::get_le(pointers + size_t(i) * 8, 8);
            if (!address || (address & 3) || !memory_.permitted(address, attr.bytes(), 4) || memory_.permitted(address, attr.bytes(), 1)) return error(0x13);
        }
        groups_[group] = {true, attr};
        for (int32_t i = 0; i < count; ++i)
            buffers_[size_t(start + i)] = {true, group, ps4::get_le(pointers + size_t(i) * 8, 8)};
        return int64_t(group);
    }
    int64_t unregister_buffers(uint64_t port, int32_t group) {
        std::lock_guard<std::mutex> lock(guard_);
        if (!valid(port)) return error(11);
        if (group < 0 || size_t(group) >= groups_.size() || !groups_[size_t(group)].used) return error(10);
        if (frame_.ticket && buffers_[frame_.buffer_index].group == size_t(group)) return error(9);
        for (auto& buffer : buffers_) if (buffer.used && buffer.group == size_t(group)) buffer = {};
        groups_[size_t(group)] = {}; return 0;
    }
    int64_t add_event(uint64_t queue, uint64_t port, uint64_t id, uint64_t user_data, bool remove = false) {
        std::lock_guard<std::mutex> lock(guard_);
        if (!valid(port)) return error(11);
        const auto result = remove ? events_.video_event_delete(queue, port, id) : events_.video_event_add(queue, port, id, user_data);
        return result == ps4::ok ? 0 : result == ps4::badf ? error(12) : error(0x100c);
    }
    int64_t submit(uint64_t port, int32_t index, int32_t mode, uint64_t argument) {
        std::lock_guard<std::mutex> lock(guard_);
        if (!valid(port)) return error(11);
        if (index < 0 || size_t(index) >= buffers_.size() || !buffers_[size_t(index)].used) return error(10);
        if (mode != 1) return error(6);
        if (frame_.ticket) return error(0x12);
        if (next_frame_ == UINT64_MAX) return error(0x17);
        const auto& buffer = buffers_[size_t(index)]; const auto& attr = groups_[buffer.group].attribute;
        try {
            std::vector<uint8_t> pixels(attr.bytes());
            if (!memory_.read(buffer.address, pixels.data(), pixels.size())) return error(0x13);
            frame_ = {next_frame_++, port_, argument, attr.width, attr.height, attr.pitch, attr.format, uint32_t(index), attr.tiled ? 1U : 0U, pixels.size()};
            pixels_.swap(pixels);
        } catch (const std::bad_alloc&) { return error(0x100c); }
        return 0;
    }
    // What sceVideoOutGetResolutionStatus reports (1920x1080 by default);
    // kept across reset, set by the host before the game runs.
    bool set_output(uint32_t width, uint32_t height) {
        if (!((width == 1920 && height == 1080) || (width == 1280 && height == 720))) return false;
        std::lock_guard<std::mutex> lock(guard_); output_width_ = width; output_height_ = height; return true;
    }
    // A flip refused because another is still pending (error 0x12) and skipped
    // by sceGnmSubmitAndFlipCommandBuffers instead of failing the submission.
    void count_dropped() { std::lock_guard<std::mutex> lock(guard_); ++dropped_; }
    uint64_t dropped() { std::lock_guard<std::mutex> lock(guard_); return dropped_; }
    // Flips the game asked for: accepted (tickets issued) plus skipped.
    uint64_t requested() { std::lock_guard<std::mutex> lock(guard_); return next_frame_ - 1 + dropped_; }
    // Metadata of the pending flip, without copying CPU surface pixels.
    int32_t target(maxps4_videoout_frame& info, uint64_t& address) {
        info={}; address=0;
        std::lock_guard<std::mutex> lock(guard_);
        if (!frame_.ticket) return -EAGAIN;
        info=frame_; address=buffers_[frame_.buffer_index].address; return 0;
    }
    int32_t snapshot(maxps4_videoout_frame& info, uint8_t* pixels, size_t capacity) {
        info = {};
        std::lock_guard<std::mutex> lock(guard_);
        if (!frame_.ticket) return -EAGAIN;
        if ((!pixels && capacity) || (pixels && capacity < pixels_.size())) return -EINVAL;
        if (pixels) std::memcpy(pixels, pixels_.data(), pixels_.size());
        info = frame_; return 0;
    }
    int64_t complete(uint64_t ticket, bool presented) {
        std::lock_guard<std::mutex> lock(guard_);
        if (!ticket || ticket != frame_.ticket) return error(1);
        if (presented) {
            ++flip_count_; last_argument_ = frame_.flip_argument; current_buffer_ = int32_t(frame_.buffer_index);
            flip_time_ = now(); events_.video_event_notify(port_, 6, last_argument_, flip_time_);
        }
        frame_ = {}; pixels_.clear(); return 0;
    }
    int64_t vblank(uint64_t port) {
        std::lock_guard<std::mutex> lock(guard_);
        if (!valid(port)) return error(11);
        ++vblank_count_; vblank_time_ = now(); events_.video_event_notify(port_, 7, 0, vblank_time_); return 0;
    }
    // sceVideoOutGetResolutionStatus: a 1080p 59.94 Hz television (shadPS4
    // layout: four s32 sizes, u64 refresh rate 3, float inches, flags, zeroes).
    int64_t resolution(uint64_t port, uint64_t output) {
        std::lock_guard<std::mutex> lock(guard_);
        if (!valid(port)) return error(11);
        uint8_t wire[44]{};
        const uint32_t sizes[] = {output_width_, output_height_, output_width_, output_height_};
        for (size_t i = 0; i < 4; ++i) ps4::put_le(wire, i * 4, sizes[i], 4);
        ps4::put_le(wire, 16, 3, 8);
        const float inches = 50.0f;
        std::memcpy(wire + 24, &inches, 4);
        return output && memory_.write(output, wire, sizeof(wire)) ? 0 : error(2);
    }
    int64_t pending(uint64_t port) {
        std::lock_guard<std::mutex> lock(guard_); return valid(port) ? (frame_.ticket ? 1 : 0) : error(11);
    }
    int64_t flip_status(uint64_t port, uint64_t output) {
        std::lock_guard<std::mutex> lock(guard_);
        if (!valid(port)) return error(11);
        uint8_t wire[64]{};
        ps4::put_le(wire, 0, flip_count_, 8); ps4::put_le(wire, 8, flip_time_, 8);
        ps4::put_le(wire, 24, last_argument_, 8); ps4::put_le(wire, 52, frame_.ticket ? 1 : 0, 4);
        ps4::put_le(wire, 56, uint32_t(current_buffer_), 4);
        return memory_.write(output, wire, sizeof(wire)) ? 0 : error(2);
    }
    int64_t vblank_status(uint64_t port, uint64_t output) {
        std::lock_guard<std::mutex> lock(guard_);
        if (!valid(port)) return error(11);
        uint8_t wire[40]{};
        ps4::put_le(wire, 0, vblank_count_, 8); ps4::put_le(wire, 8, vblank_time_, 8);
        return memory_.write(output, wire, sizeof(wire)) ? 0 : error(2);
    }
    int64_t event_field(uint64_t address, uint64_t output, uint32_t field) {
        uint8_t event[32]{};
        if (!memory_.read(address, event, sizeof(event))) return error(2);
        const uint64_t id = ps4::get_le(event, 8), data = ps4::get_le(event + 16, 8);
        if (ps4::get_le(event + 8, 2) != 0xfff3 || (id != 6 && id != 7)) return error(13);
        if (field == 0) return id == 6 ? 0 : 1;
        if (field == 2) return int64_t((data >> 12) & 15);
        uint64_t argument = data >> 16;
        if (id == 6 && (data >> 63)) argument |= 0xffff000000000000ULL;
        uint8_t wire[8]; ps4::put_le(wire, 0, argument, 8);
        return memory_.write(output, wire, sizeof(wire)) ? 0 : error(2);
    }
private:
    struct Attribute { uint32_t format = 0, width = 0, height = 0, pitch = 0; bool tiled = false; size_t bytes() const { return size_t(pitch) * height * 4; } };
    struct Group { bool used = false; Attribute attribute; };
    struct Buffer { bool used = false; size_t group = 0; uint64_t address = 0; };
    PS4GuestMemory& memory_; PS4Threads& events_; std::mutex guard_;
    std::array<Group, group_limit> groups_{}; std::array<Buffer, buffer_limit> buffers_{};
    uint64_t port_ = 0, next_port_ = 1, next_frame_ = 1, flip_count_ = 0, vblank_count_ = 0, dropped_ = 0;
    uint64_t last_argument_ = UINT64_MAX, flip_time_ = 0, vblank_time_ = 0;
    int32_t current_buffer_ = -1;
    uint32_t output_width_ = 1920, output_height_ = 1080;
    maxps4_videoout_frame frame_{}; std::vector<uint8_t> pixels_;
    bool valid(uint64_t port) const { return port && port == port_; }
    static uint64_t now() {
        return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    void clear() {
        if (port_) events_.video_event_remove_source(port_);
        port_ = 0; frame_ = {}; pixels_.clear(); groups_.fill({}); buffers_.fill({});
        flip_count_ = vblank_count_ = flip_time_ = vblank_time_ = dropped_ = 0; last_argument_ = UINT64_MAX; current_buffer_ = -1;
    }
};
}
