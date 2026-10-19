#include "../Native/PS4ExecutableImage.hpp"
#include "../Native/PM4GuestSubmission.hpp"
#include "GuestELFFixture.h"
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <string>

#include "PS4ExecutableFixture.h"
// Original fixture: clear SELF with an explicitly described version trailer.
static std::vector<uint8_t> selfWithVersions() {
    auto b=plainSELF();
    b.insert(b.begin()+288,64,0); // Space for the third ELF program header.
    guest_test::put(b,352,12,2);
    guest_test::put(b,363,40,8); guest_test::put(b,352,72,8);
    guest_test::put(b,3,96+56,2);
    const size_t ph=96+64+2*56;
    std::fill(b.begin()+ph,b.begin()+ph+56,0);
    guest_test::put(b,0x6fffff01,ph,4);
    guest_test::put(b,0x20000,ph+8,8);
    guest_test::put(b,17,ph+32,8); guest_test::put(b,1,ph+48,8);
    guest_test::put(b,b.size(),16,4);
    const uint8_t version[]={16,'t','e','s','t','_','m','o','d','u','l','e',':',4,0,0x80,0x61};
    b.insert(b.end(),std::begin(version),std::end(version));
    return b;
}
int main() {
    maxps4::PS4ExecutableImage image;
    auto elf = ps4ELF();
    assert(image.load(elf.data(), elf.size()) == 0);
    assert(!image.isSelf && image.entry == 0x1000 && image.type == 0xfe00);
    assert(image.segments.size() == 2 && !image.requiresLinker && !image.requiresTLS);
    assert(image.translate(0x1000, 11, 5)[0] == 0xb8);
    assert(image.translate(0x1000, 1, 2) == nullptr);
    assert(image.translate(0x10000, 64, 6)[12] == 0); // Zero-filled BSS.
    auto commands = maxps4::gpu::snapshotPM4(image, 0x10000, 3);
    assert(commands.error == 0 && commands.words[2] == 2);
    auto sectionless=elf; guest_test::put(sectionless,64,58,2);
    assert(image.load(sectionless.data(),sectionless.size())==0); // Legal entry size without sections.
    auto self = plainSELF();
    assert(image.load(self.data(), self.size()) == 0);
    assert(image.isSelf && image.entry == 0x1000 && image.segments.size() == 2);
    assert(image.translate(0x1000, 11, 5)[0] == 0xb8);
    assert(image.translate(0x10000, 64, 6)[12] == 0);
    commands = maxps4::gpu::snapshotPM4(image, 0x10000, 3);
    assert(commands.error == 0 && commands.words[0] == 0xc0012d00);
    auto reject = [&](const std::vector<uint8_t>& original, size_t offset, uint64_t value, size_t width) {
        auto bad = original; guest_test::put(bad, value, offset, width);
        assert(image.load(bad.data(), bad.size()) != 0);
        assert(image.entry == 0 && image.segments.empty() && image.headers.empty());
    };
    reject(self, 5, 2, 1); reject(self, 7, 0, 1);
    reject(self, 24, 129, 2); reject(self, 16, self.size() + 1, 4);
    reject(self, 12, 64, 2); reject(self, 40, UINT64_MAX, 8);
    reject(self, 48, 13, 8); reject(self, 56, 13, 8);
    reject(self, 32, 0x200800, 8); // Block ID outside program header table.
    reject(self, 64, 0x100800, 8); // Duplicate block for program header 1.
    reject(self, 32, 0x100802, 8); // Encrypted: no substitute/decryption.
    reject(self, 32, 0x100808, 8); // Compressed: no guessed layout.
    reject(self, 32, 0x100810, 8); // Unknown flags.
    reject(elf, 9, 1, 1); reject(elf, 15, 1, 1); reject(elf, 48, 1, 4);
    reject(elf, 40, 64, 8); reject(elf, 58, 56, 2); reject(elf, 62, 1, 2);
    // Metadata file spans are validated even when not mapped into guest memory.
    auto badMetadata=elf; guest_test::put(badMetadata,2,120,4);
    reject(badMetadata,128,UINT64_MAX,8);
    reject(elf, 7, 0, 1); reject(elf, 16, 2, 2); reject(elf, 18, 183, 2);
    reject(elf, 32, UINT64_MAX, 8); reject(elf, 56, 129, 2);
    reject(elf, 24, 0x10000, 8); reject(elf, 96, 33, 8);
    reject(elf, 104, maxps4::PS4ExecutableImage::memoryBudget + 1, 8);
    reject(elf, 136, 0x1001, 8); reject(elf, 80, UINT64_MAX - 2, 8);
    reject(elf, 72, UINT64_MAX, 8); reject(elf, 112, 3, 8);
    for (size_t length = 0; length < 272; ++length)
        assert(image.load(self.data(), length) != 0 && image.segments.empty());
    // Identical errno, distinct stage; no stale state from an earlier success.
    const auto stagedReject = [&](std::vector<uint8_t> bad, const char* stage, int status) {
        assert(image.load(elf.data(),elf.size()) == 0);
        assert(image.load(bad.data(),bad.size()) == status);
        assert(std::string(image.stage) == stage && image.entry == 0 && image.segments.empty() && image.headers.empty());
    };
    auto bad = elf; std::memcpy(bad.data(),"\177CNT",4);
    stagedReject(bad,"ELF.CONTAINER",-ENOEXEC);
    bad = elf; guest_test::put(bad,183,18,2); stagedReject(bad,"ELF.IDENTITY",-ENOEXEC);
    bad = elf; guest_test::put(bad,2,16,2); stagedReject(bad,"ELF.TYPE",-ENOEXEC);
    bad = elf; guest_test::put(bad,UINT64_MAX,32,8); stagedReject(bad,"ELF.PROGRAM_TABLE",-ENOEXEC);
    bad = elf; guest_test::put(bad,0x10000,24,8); stagedReject(bad,"ELF.ENTRY",-ENOEXEC);
    bad = self; guest_test::put(bad,0x100802,32,8); stagedReject(bad,"SELF.ENCRYPTED",-ENOTSUP);
    auto versioned=selfWithVersions();
    const size_t extent=375;
    assert(image.load(versioned.data(),versioned.size())==0 && image.isSelf);
    assert(image.segments.size()==2 && image.headers.size()==3);
    assert(image.translate(0x1000,11,5)[0]==0xb8);
    assert(!image.translate(0x20000,1,4)); // Trailer is never guest memory.
    const auto trailerReject=[&](std::vector<uint8_t> bytes) {
        stagedReject(std::move(bytes),"SELF.TRAILER",-ENOEXEC);
    };
    bad=versioned; bad.push_back(0); trailerReject(bad); // Undescribed extra byte.
    bad=versioned; bad[extent]=0; trailerReject(bad); // Zero record length.
    bad=versioned; bad[extent]=255; trailerReject(bad); // Record extends past EOF.
    bad=versioned; bad[extent+12]='!'; trailerReject(bad); // Missing name separator.
    bad=versioned; bad[extent+1]=1; trailerReject(bad); // Nonprintable name.
    bad=versioned; guest_test::put(bad,0,272+8,8); trailerReject(bad); // Logical version offset overlaps prior data.
    bad=versioned; guest_test::put(bad,UINT64_MAX,272+8,8); trailerReject(bad);
    bad=versioned; guest_test::put(bad,16,272+32,8); trailerReject(bad);
    bad=versioned; guest_test::put(bad,1,272+40,8); trailerReject(bad); // Trailer cannot be loadable memory.
    bad=versioned; guest_test::put(bad,1,272+4,4); trailerReject(bad);
    bad=versioned; guest_test::put(bad,0,272,4); trailerReject(bad); // No descriptor.
    bad=versioned; std::copy(bad.begin()+272,bad.begin()+328,bad.begin()+216); trailerReject(bad); // Duplicate.
    for (size_t size=extent+1;size<versioned.size();++size) {
        bad=versioned; bad.resize(size); trailerReject(bad);
    }
    bad=versioned; guest_test::put(bad,extent,40,8);
    stagedReject(bad,"SELF.BLOCK",-ENOEXEC); // Source fits host file but crosses declared SELF extent.
    bad=versioned; guest_test::put(bad,200,12,2);
    stagedReject(bad,"SELF.TRAILER",-ENOEXEC); // Program table must stay inside header, not metadata.
    bad=versioned; guest_test::put(bad,0x100802,32,8);
    stagedReject(bad,"SELF.ENCRYPTED",-ENOTSUP); // Encrypted still refused after a valid trailer.
    bad=versioned; guest_test::put(bad,0x100804,32,8);
    stagedReject(bad,"SELF.BLOCK",-ENOTSUP); // Signature policy stays explicit.
    bad=versioned; guest_test::put(bad,0x100808,32,8);
    stagedReject(bad,"SELF.COMPRESSED_UNSUPPORTED",-ENOTSUP); // Compression policy unchanged.
    // A PS4 library has no required entry. Dynamic/TLS metadata is retained,
    // explicitly marked as unresolved, and never passed to FEX by this class.
    guest_test::put(elf, 0xfe18, 16, 2); guest_test::put(elf, 0, 24, 8);
    guest_test::put(elf, 2, 120, 4);
    assert(image.load(elf.data(), elf.size()) == 0 && image.requiresLinker && image.headers.size() == 2);
    guest_test::put(elf, 7, 120, 4);
    assert(image.load(elf.data(), elf.size()) == 0 && image.requiresTLS);
    std::puts("PASS real PS4 ELF identity and plaintext SELF block-ID mapping, BSS, PM4 read, bounds/rejections");
    std::puts("Segment staging only: no PS4 imports, relocations, TLS initialization, HLE or execution");
}
