#include "../Native/PM4Type3Decoder.hpp"
#include <cassert>
#include <cstdint>
using namespace maxps4::gpu;
int main() {
    const std::uint32_t commands[] = {
        (3u << 30) | (1u << 16) | (0x2du << 8), 12u, 34u,
        (3u << 30) | (0x10u << 8), 56u
    };
    auto ok = decodePM4Type3(commands, 5);
    assert(ok && ok.packets.size() == 2);
    assert(ok.packets[0].opcode == 0x2d && ok.packets[0].payloadCount == 2);
    assert(ok.packets[1].opcode == 0x10 && ok.packets[1].payloadWord == 4);
    assert(decodePM4Type3(commands, 2).error == PM4Error::truncated_packet);
    assert(decodePM4Type3(nullptr, 1).error == PM4Error::null_input);
    assert(decodePM4Type3(commands, 5, 1).error == PM4Error::too_many_packets);
    const std::uint32_t invalid[] = {0u};
    assert(decodePM4Type3(invalid, 1).error == PM4Error::unsupported_packet_type);
    assert(decodePM4Type3(nullptr, 0));
}
