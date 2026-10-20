#pragma once
#include "OrbisMetadata.hpp"
#include "OrbisV4SystemInventory.hpp"
#include <tuple>
namespace maxps4 {
// Read-only metadata audit. Never replaces OrbisLinker, validates relocations,
// maps memory, discovers arbitrary files or authorizes CPU dispatch.
struct OrbisV4DependencyDiagnostic {
    enum class Coverage { Unknown, Partial, Complete };
    enum class Behavior { Unknown, ImplementedProfile, PartialProfile, ExplicitRefusal };
    struct Function {
        std::string nid,library,module,name;
        uint16_t libraryVersion=1,moduleVersion=0x101;
        uint8_t type=2;
        Behavior behavior=Behavior::Unknown;
    };
    struct Input {std::string name;const uint8_t* bytes;size_t size;Coverage coverage=Coverage::Unknown;std::vector<Function> functions={};};
    struct Result {
        size_t missing=0,unresolved=0,matched=0,descriptors=0,invalid=0,duplicates=0,cycles=0;
        size_t implemented=0,partial=0,refused=0,unverified=0;
        std::string text;
    };
    static const char* coverageName(Coverage c) {
        return c==Coverage::Partial ? "partial" : c==Coverage::Complete ? "complete-declared" : "unverified";
    }
    static const char* behaviorName(Behavior b){
        return b==Behavior::ImplementedProfile ? "implemented-profile" : b==Behavior::PartialProfile ? "partial-profile" :
            b==Behavior::ExplicitRefusal ? "explicit-refusal" : "unverified";
    }
    static Result inspect(std::vector<Input> inputs) {
        Result out;
        auto q=[](const std::string& s){return OrbisV4SystemInventory::quote(s);};
        auto line=[&](const std::string& s){
            if(out.text.size()+s.size()+1>4*1024*1024)throw std::length_error("dependency diagnostic output budget");
            out.text+=s+'\n';
        };
        line("[ORBIS.GLOBAL] metadata-only; admission=false; linkage=not-validated; scope=supplied-files");
        if(inputs.empty() || inputs.size()>128){out.invalid=1;line("invalid module count");return out;}
        std::stable_sort(inputs.begin(),inputs.end(),[](const auto& a,const auto& b){return a.name<b.name;});
        std::vector<OrbisMetadata> metadata(inputs.size());std::vector<bool> valid(inputs.size(),false);
        std::map<std::string,std::vector<size_t>> names;
        size_t bytes=0,records=0;
        for(size_t i=0;i<inputs.size();++i){
            const auto& in=inputs[i];
            if(in.name.empty() || in.name.size()>255 || !in.bytes || in.size>32*1024*1024-bytes){
                ++out.invalid;line("provider="+q(in.name.substr(0,255))+" status=invalid-input-or-budget");continue;
            }
            bytes+=in.size;names[in.name].push_back(i);
            auto& m=metadata[i];
            if(!m.parse(in.bytes,in.size)){++out.invalid;line("provider="+q(in.name)+" status=invalid-metadata reason="+q(m.error));continue;}
            const size_t count=m.symbols.size()+m.modules.size()+m.libraries.size()+m.needed.size();
            if(count>65536-records){++out.invalid;line("provider="+q(in.name)+" status=record-budget");continue;}
            if(in.functions.size()>256){++out.invalid;line("provider="+q(in.name)+" status=function-contract-budget");continue;}
            records+=count;valid[i]=true;
            line("provider="+q(in.name)+" status=accessible coverage="+coverageName(in.coverage));
        }
        for(const auto& entry:names)if(entry.second.size()>1){++out.duplicates;line("duplicate-provider="+q(entry.first));}
        using Key=std::tuple<std::string,std::string,std::string,uint16_t,uint16_t,uint8_t>;
        std::vector<std::map<Key,Function>> contracts(inputs.size());
        using Base=std::tuple<std::string,std::string,std::string>;
        auto key=[](const OrbisMetadata::Symbol& s){return Key{s.nid,s.library,s.module,s.libraryVersion,s.moduleVersion,s.kind};};
        auto base=[](const OrbisMetadata::Symbol& s){return Base{s.nid,s.library,s.module};};
        std::map<Key,std::vector<size_t>> exports;std::set<Base> exportNames;
        using Descriptor=std::pair<std::string,uint16_t>;
        std::map<Descriptor,std::vector<size_t>> modules,libraries;
        std::set<std::string> moduleNames,libraryNames;
        for(size_t i=0;i<inputs.size();++i)if(valid[i]){
            const auto& m=metadata[i];
            for(const auto& segment:m.segments)if(segment.type==7)
                line("load-requirement provider="+q(inputs[i].name)+" feature=per-thread-tls initial_bytes="+
                    std::to_string(segment.filesz)+" total_bytes="+std::to_string(segment.memsz)+" admission=false");
            if(m.init || m.fini)line("load-requirement provider="+q(inputs[i].name)+
                " feature=module-initializers init="+std::to_string(m.init)+" fini="+std::to_string(m.fini)+" admission=false");
            std::map<uint32_t,size_t> unsupportedRelocations;
            for(const auto& r:m.relocations)if(!r.supported)++unsupportedRelocations[r.type];
            for(const auto& r:unsupportedRelocations)line("load-requirement provider="+q(inputs[i].name)+
                " feature=unsupported-relocation type="+std::to_string(r.first)+" count="+std::to_string(r.second)+" admission=false");
            for(const auto& f:inputs[i].functions){
                const Key identity{f.nid,f.library,f.module,f.libraryVersion,f.moduleVersion,f.type};
                // A behavioral declaration is never an export or a resolver.
                if(!contracts[i].emplace(identity,f).second)throw std::invalid_argument("duplicate function contract");
            }
            for(const auto& s:m.symbols)if(s.defined && s.binding && s.qualified){exports[key(s)].push_back(i);exportNames.insert(base(s));}
            for(const auto& s:m.symbols)if(s.defined && s.binding && s.qualified){
                const auto f=contracts[i].find(key(s));
                line("function provider="+q(inputs[i].name)+" nid="+q(s.nid)+" library="+q(s.library)+
                    " module="+q(s.module)+" name="+q(f==contracts[i].end() ? "" : f->second.name)+
                    " behavior="+behaviorName(f==contracts[i].end() ? Behavior::Unknown : f->second.behavior)+" execution-observed=false");
            }
            for(const auto& d:m.modules)if(d.exported){modules[{d.name,d.version}].push_back(i);moduleNames.insert(d.name);}
            for(const auto& d:m.libraries)if(d.exported){libraries[{d.name,d.version}].push_back(i);libraryNames.insert(d.name);}
        }
        std::set<std::string> missing;
        std::vector<std::set<size_t>> edges(inputs.size());std::vector<size_t> indegree(inputs.size(),0);
        for(size_t i=0;i<inputs.size();++i)if(valid[i]){
            const auto& m=metadata[i];
            auto declared=[&](size_t owner){return owner==i || std::find(m.needed.begin(),m.needed.end(),inputs[owner].name)!=m.needed.end();};
            for(const auto& dep:m.needed){
                const auto found=names.find(dep);std::string status;
                if(found==names.end()){missing.insert(dep);status="missing";}
                else if(found->second.size()!=1)status="ambiguous";
                else if(!valid[found->second.front()])status="invalid-provider";
                else {
                    const auto owner=found->second.front();status="accessible";
                    if(edges[owner].insert(i).second)++indegree[i];
                }
                line("dependency consumer="+q(inputs[i].name)+" name="+q(dep)+" status="+status);
            }
            auto descriptor=[&](const OrbisMetadata::Identity& d,bool library){
                if(d.exported)return;
                const auto& registry=library ? libraries : modules;
                const auto& allNames=library ? libraryNames : moduleNames;
                const auto it=registry.find({d.name,d.version});std::string reason;
                if(it==registry.end())reason=allNames.count(d.name) ? "version-mismatch" : "missing-provider";
                else if(it->second.size()!=1)reason="ambiguous-provider";
                else if(std::find(m.needed.begin(),m.needed.end(),inputs[it->second.front()].name)==m.needed.end())reason="undeclared-provider";
                if(!reason.empty()){
                    ++out.descriptors;line("descriptor consumer="+q(inputs[i].name)+" kind="+(library ? "library" : "module")+
                        " name="+q(d.name)+" version="+std::to_string(d.version)+" status="+reason);
                }
            };
            for(const auto& d:m.modules)descriptor(d,false);
            for(const auto& d:m.libraries)descriptor(d,true);
            for(const auto& s:m.symbols)if(!s.defined && !s.encoded.empty()){
                std::string reason;size_t owner=SIZE_MAX;
                const auto it=exports.find(key(s));
                if(!s.qualified)reason="unsupported-unqualified-identity";
                else if(it==exports.end())reason=exportNames.count(base(s)) ? "version-or-type-mismatch" : "missing-export";
                else if(it->second.size()!=1)reason="ambiguous-export";
                else if(!declared(it->second.front()))reason="undeclared-provider";
                else owner=it->second.front();
                Behavior behavior=Behavior::Unknown;std::string function;
                if(reason.empty()){
                    ++out.matched;const auto contract=contracts[owner].find(key(s));
                    if(contract!=contracts[owner].end()){behavior=contract->second.behavior;function=contract->second.name;}
                    if(behavior==Behavior::ImplementedProfile)++out.implemented;
                    else if(behavior==Behavior::PartialProfile)++out.partial;
                    else if(behavior==Behavior::ExplicitRefusal)++out.refused;
                    else ++out.unverified;
                }else ++out.unresolved;
                line("import consumer="+q(inputs[i].name)+" nid="+q(s.nid)+" library="+q(s.library)+
                    " module="+q(s.module)+" library_version="+std::to_string(s.libraryVersion)+
                    " module_version="+std::to_string(s.moduleVersion)+" type="+std::to_string(s.kind)+
                    " status="+(reason.empty() ? "metadata-match" : reason)+
                    (owner==SIZE_MAX ? "" : " provider="+q(inputs[owner].name)+" function="+q(function)+
                        " behavior="+behaviorName(behavior))+" execution-observed=false");
            }
        }
        std::vector<size_t> ready;
        for(size_t i=0;i<inputs.size();++i)if(valid[i] && !indegree[i])ready.push_back(i);
        for(size_t p=0;p<ready.size();++p)for(auto consumer:edges[ready[p]])if(!--indegree[consumer])ready.push_back(consumer);
        for(size_t i=0;i<inputs.size();++i)if(valid[i] && indegree[i]){
            ++out.cycles;line("cycle-or-dependent="+q(inputs[i].name));
        }
        out.missing=missing.size();
        for(const auto& dep:missing)line("missing-library="+q(dep));
        line("[ORBIS.GLOBAL] missing="+std::to_string(out.missing)+" unresolved="+std::to_string(out.unresolved)+
            " metadata_matches="+std::to_string(out.matched)+" descriptor_errors="+std::to_string(out.descriptors)+
            " invalid="+std::to_string(out.invalid)+" duplicates="+std::to_string(out.duplicates)+
            " cycle_or_dependants="+std::to_string(out.cycles)+
            " implemented_profile="+std::to_string(out.implemented)+" partial_profile="+std::to_string(out.partial)+
            " explicit_refusals="+std::to_string(out.refused)+" unverified_matches="+std::to_string(out.unverified)+" admission=false");
        return out;
    }
};
}
