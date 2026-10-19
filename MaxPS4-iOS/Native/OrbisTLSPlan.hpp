#pragma once
#include "OrbisClearImage.hpp"
#include "OrbisMetadata.hpp"
#include "OrbisV4TLSExtract.hpp"
#include <set>

namespace maxps4 {
// Offline, transactional TLS phase. No import resolution, process admission or
// TCB/DTV ABI is implied. Original RELA entries are retained (never hidden).
struct OrbisTLSPlan {
    struct Input { std::string name; const uint8_t* bytes; size_t size; uint32_t id; };
    struct Patch { uint64_t target; size_t fileOffset; uint32_t module; };
    struct Module { std::string name; uint32_t id; std::vector<uint8_t> elf; std::vector<Patch> patches; };
    struct Plan {
        std::vector<Module> modules;
        std::vector<OrbisV4TLSModule> templates;
        std::vector<OrbisV4TLSPlacement> placements;
        size_t total=0, relocated=0;
    };
    static int prepare(const std::vector<Input>& inputs,Plan& output,std::string& reason) {
        auto fail=[&](int status,const std::string& text){reason="TLS.PREPARE: "+text+"; CPU_admitted=false";return status;};
        try {
            if(inputs.empty() || inputs.size()>OrbisV4TLSLayout::max_modules)return fail(-EINVAL,"invalid module count");
            Plan pending;std::set<uint32_t> ids;std::set<std::string> names;size_t bytes=0;
            for(const auto& in:inputs) {
                if(in.name.empty() || in.name.size()>255 || !in.id || in.id>128 ||
                   !ids.insert(in.id).second || !names.insert(in.name).second)
                    return fail(-EINVAL,"explicit unique module identities required");
                Module module{in.name,in.id,{},{}};std::string diagnostic;
                const int normalized=OrbisClearImage::normalize(in.bytes,in.size,module.elf,diagnostic);
                if(normalized)return fail(normalized,in.name+": "+diagnostic);
                if(module.elf.size()>32*1024*1024-bytes)return fail(-E2BIG,"normalized batch budget");
                bytes+=module.elf.size();
                OrbisMetadata metadata;
                if(!metadata.parse(module.elf.data(),module.elf.size()))return fail(-ENOEXEC,in.name+": "+metadata.error);
                PS4ExecutableImage image;
                if(image.load(module.elf.data(),module.elf.size()))return fail(-ENOEXEC,in.name+": invalid staged ELF");
                const auto tls=std::find_if(metadata.segments.begin(),metadata.segments.end(),[](const auto& s){return s.type==7;});
                const bool nonempty=tls!=metadata.segments.end() && tls->memsz;
                if(nonempty) {
                    OrbisV4TLSModule value;
                    const int extracted=OrbisV4TLSExtract::fromPlainELF(image,module.elf.data(),module.elf.size(),in.id,value);
                    if(extracted)return fail(extracted,in.name+": TLS template/align/range invalid");
                    pending.templates.push_back(std::move(value));
                }
                for(const auto& r:metadata.relocations) {
                    // Mission 49: DTPMOD64/DTPOFF64 against a real STT_TLS symbol
                    // (local or imported) are resolved and written by the linker.
                    const bool tlsSymbol=r.symbol && r.symbol<metadata.symbols.size() && metadata.symbols[r.symbol].kind==6;
                    if((r.type==16 || r.type==17) && tlsSymbol)continue;
                    if(r.type!=16) {
                        if((r.type>=17 && r.type<=23) || (r.type>=34 && r.type<=36))return fail(-ENOTSUP,in.name+": other TLS relocation unsupported");
                        continue;
                    }
                    if(!nonempty || r.addend || r.symbol>=metadata.symbols.size())
                        return fail(-ENOTSUP,in.name+": DTPMOD64 requires local TLS and zero addend");
                    const auto& symbol=metadata.symbols[r.symbol];
                    // The observed Orbis local section-marker profile. Do not
                    // assign the consuming module to imported/global TLS symbols.
                    const bool section=symbol.kind==3 && !symbol.binding && !symbol.defined &&
                        symbol.encoded.empty() && !symbol.value && !symbol.size && !symbol.visibility;
                    if(r.symbol!=0 && !section)return fail(-ENOTSUP,in.name+": nonlocal DTPMOD64 symbol unsupported");
                    const OrbisMetadata::Segment* target=nullptr;
                    for(const auto& s:metadata.segments) if((s.type==1 || s.type==0x61000010) &&
                        (s.flags&7)==6 && r.target>=s.address && OrbisMetadata::range(r.target-s.address,8,s.filesz)) {
                        if(target)return fail(-ENOEXEC,"ambiguous TLS relocation target");
                        target=&s;
                    }
                    if(tls->filesz && r.target<tls->address+tls->filesz && tls->address<r.target+8)
                        return fail(-ENOTSUP,in.name+": TLS template relocation requires ordered initialization");
                    if(!target || r.target%8)return fail(-ENOEXEC,in.name+": TLS target must be aligned file-backed RW/NX");
                    module.patches.push_back({r.target,size_t(target->offset+r.target-target->address),in.id});
                }
                pending.modules.push_back(std::move(module));
            }
            if(!pending.templates.empty()) {
                const int planned=OrbisV4TLSLayout::plan(pending.templates,pending.placements,pending.total);
                if(planned)return fail(planned,"TLS layout rejected");
            }
            // All modules, targets and layouts are validated before writes.
            for(auto& module:pending.modules)for(const auto& patch:module.patches) {
                for(size_t i=0;i<8;++i)module.elf.at(patch.fileOffset+i)=uint8_t(uint64_t(patch.module)>>(8*i));
                ++pending.relocated;
            }
            output=std::move(pending);
            reason="TLS.PREPARE: DTPMOD64 applied="+std::to_string(output.relocated)+
                "; offline copies only; imports/init/process TCB-DTV unresolved; CPU_admitted=false";
            return 0;
        } catch(const std::bad_alloc&) {return fail(-ENOMEM,"allocation failed");}
          catch(const std::exception& e) {return fail(-ENOEXEC,e.what());}
    }
};
}
