#include "../Native/OrbisV4TLSExtract.hpp"
#include "GuestELFFixture.h"
#include "PS4ExecutableFixture.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace maxps4;
    auto elf = ps4ELF();
    // Append one PT_TLS program header in existing PH table only if room exists:
    // build a fresh table at EOF, keeping original loadable segments untouched.
    const size_t old_ph = 64;
    const size_t old_count = 2;
    const size_t new_ph = elf.size();
    const size_t tls_offset = (new_ph + 3 * 56 + 15) & ~size_t(15);
    elf.resize(tls_offset + 64, 0);
    for (size_t i = 0; i < old_count * 56; ++i) elf[new_ph + i] = elf[old_ph + i];
    guest_test::put(elf, new_ph, 32, 8);
    guest_test::put(elf, 3, 56, 2);
    const size_t ph = new_ph + old_count * 56;
    guest_test::put(elf, 7, ph, 4);
    guest_test::put(elf, tls_offset, ph + 8, 8);
    guest_test::put(elf, 0x8000, ph + 16, 8);
    guest_test::put(elf, 4, ph + 32, 8);
    guest_test::put(elf, 64, ph + 40, 8);
    guest_test::put(elf, 16, ph + 48, 8);
    elf[tls_offset] = 0x12;
    elf[tls_offset + 1] = 0x34;
    PS4ExecutableImage image;
    assert(image.load(elf.data(), elf.size()) == 0 && image.requiresTLS);
    OrbisV4TLSModule module;
    assert(OrbisV4TLSExtract::fromPlainELF(image, elf.data(), elf.size(), 1, module) == 0);
    assert(module.memory_size == 64 && module.initial.size() == 4);
    assert(module.initial[0] == 0x12 && module.initial[1] == 0x34);
    auto bad = module;
    assert(OrbisV4TLSExtract::fromPlainELF(image, elf.data(), elf.size() - 64, 1, bad) != 0);
    assert(bad.initial == module.initial); // transactional failure
    std::vector<OrbisV4TLSPlacement> placements;
    size_t total = 0;
    assert(OrbisV4TLSLayout::plan({module}, placements, total) == 0);
    std::vector<uint8_t> bytes;
    assert(OrbisV4TLSLayout::instantiate({module}, placements, total, bytes) == 0);
    assert(bytes.size() == 64 && bytes[0] == 0x12 && bytes[1] == 0x34 && bytes[4] == 0);
    std::cout << "PASS PT_TLS plaintext extraction and layout\n";
}
