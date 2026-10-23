#include "../Native/OrbisMetadata.hpp"
#include "../Native/OrbisLinker.hpp"
#include "PS4OrbisExecutionFixture.h"
#include "SelfFixture.h"
#include <cassert>
#include <cstdio>

// SELF with plaintext blocks read as its plain ELF (shadPS4 loader layout);
// encrypted, deflated and malformed containers refused.
int main() {
    using maxps4::SelfImage;
    const auto provider = orbis_execution::make(true), app = orbis_execution::make(false);
    std::vector<uint8_t> plain;
    std::string why;
    assert(SelfImage::unwrap(provider.data(), provider.size(), plain, why) == 0 && plain.empty());
    for (const auto* elf : {&provider, &app}) {
        const auto self = self_fixture::wrap(*elf);
        assert(SelfImage::isSelf(self.data(), self.size()) && !SelfImage::encrypted(self.data(), self.size()));
        assert(SelfImage::unwrap(self.data(), self.size(), plain, why) == 1 && why.empty());
        // Same metadata as the ELF itself, and the program bytes in place.
        maxps4::OrbisMetadata original, wrapped;
        assert(original.parse(elf->data(), elf->size()) && wrapped.parse(self.data(), self.size()));
        assert(wrapped.container && !original.container);
        assert(wrapped.type == original.type && wrapped.entry == original.entry);
        assert(wrapped.symbols.size() == original.symbols.size() && wrapped.relocations.size() == original.relocations.size());
        assert(wrapped.modules.size() == original.modules.size() && wrapped.libraries.size() == original.libraries.size());
        for (const auto& s : original.segments)
            if (s.filesz) assert(!std::memcmp(plain.data() + s.offset, elf->data() + s.offset, size_t(s.filesz)));
        assert(wrapped.report().find("SELF à blocs en clair") != std::string::npos);
    }
    // The linker takes the unwrapped modules as it takes the ELF files.
    std::vector<uint8_t> appPlain, providerPlain;
    const auto appSelf = self_fixture::wrap(app), providerSelf = self_fixture::wrap(provider);
    assert(SelfImage::unwrap(appSelf.data(), appSelf.size(), appPlain, why) == 1);
    assert(SelfImage::unwrap(providerSelf.data(), providerSelf.size(), providerPlain, why) == 1);
    using Input = maxps4::OrbisLinker::Input;
    const std::vector<Input> direct{{{{"app.elf", {"provider.prx"}}, app.data(), app.size()}, orbis_execution::app_bias},
        {{{"provider.prx", {}}, provider.data(), provider.size()}, orbis_execution::provider_bias}};
    const std::vector<Input> unwrapped{{{{"app.elf", {"provider.prx"}}, appPlain.data(), appPlain.size()}, orbis_execution::app_bias},
        {{{"provider.prx", {}}, providerPlain.data(), providerPlain.size()}, orbis_execution::provider_bias}};
    maxps4::OrbisLinker::Image fromElf, fromSelf;
    std::string reason;
    assert(maxps4::OrbisLinker::link(direct, {}, fromElf, reason));
    assert(maxps4::OrbisLinker::link(unwrapped, {}, fromSelf, reason));
    assert(fromElf.modules.size() == fromSelf.modules.size());
    for (size_t i = 0; i < fromElf.modules.size(); ++i)
        assert(fromElf.modules[i].entry == fromSelf.modules[i].entry && fromElf.modules[i].bias == fromSelf.modules[i].bias);
    // Refused: encrypted, deflated, block outside the file, bad header.
    const auto encrypted = self_fixture::wrap(provider, 0x2);
    assert(SelfImage::encrypted(encrypted.data(), encrypted.size()));
    assert(SelfImage::unwrap(encrypted.data(), encrypted.size(), plain, why) == -ENOTSUP && plain.empty());
    assert(why.find("chiffré") != std::string::npos);
    maxps4::OrbisMetadata refused;
    assert(!refused.parse(encrypted.data(), encrypted.size()) && refused.error.find("chiffré") != std::string::npos);
    const auto deflated = self_fixture::wrap(provider, 0x8);
    assert(!SelfImage::encrypted(deflated.data(), deflated.size()));
    assert(SelfImage::unwrap(deflated.data(), deflated.size(), plain, why) == -ENOTSUP && why.find("compressé") != std::string::npos);
    auto outside = self_fixture::wrap(provider);
    self_fixture::set(outside, 40, outside.size(), 8);
    assert(SelfImage::unwrap(outside.data(), outside.size(), plain, why) == -ENOEXEC);
    auto header = self_fixture::wrap(provider);
    header[7] = 0;
    assert(SelfImage::unwrap(header.data(), header.size(), plain, why) == -ENOEXEC);
    const auto good = self_fixture::wrap(provider);
    for (size_t length = 0; length <= good.size(); length += 7) // Every truncation stays bounded.
        (void)SelfImage::unwrap(good.data(), length, plain, why);
    std::puts("PS4 SELF container (plaintext blocks) checks passed");
}
