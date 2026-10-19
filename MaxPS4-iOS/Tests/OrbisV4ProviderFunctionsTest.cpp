#include "../Native/OrbisV4ProviderFunctions.hpp"
#include <cassert>
#include <iostream>
int main(){
    using namespace maxps4;
    std::vector<std::pair<std::string,std::vector<uint8_t>>> providers{
        {OrbisV4AppContent::path,OrbisV4AppContent::provider()},
        {OrbisV4AudioOut::path,OrbisV4AudioOut::provider()},
        {OrbisV4CommonDialog::path,OrbisV4CommonDialog::provider()},
        {OrbisV4ErrorDialog::path,OrbisV4ErrorDialog::provider()},
        {OrbisV4GnmDriver::path,OrbisV4GnmDriver::provider()},
        {OrbisV4NpCommon::path,OrbisV4NpCommon::provider()},
        {OrbisV4NpManager::path,OrbisV4NpManager::provider()},
        {OrbisV4LibcInternal::path,OrbisV4LibcInternal::provider()},
        {OrbisV4NpTrophy::path,OrbisV4NpTrophy::provider()}};
    for(const auto& m:OrbisV4LocalServices::modules())providers.push_back({m.path(),OrbisV4LocalServices::provider(m)});
    for(const auto& p:providers){
        OrbisMetadata m;assert(m.parse(p.second.data(),p.second.size()));
        const auto contracts=OrbisV4ProviderFunctions::get(p.first);
        assert(contracts.size()==m.symbols.size()-1);
        for(const auto& s:m.symbols)if(s.defined){
            size_t count=0;
            for(const auto& f:contracts)if(f.nid==s.nid && f.library==s.library && f.module==s.module &&
                f.libraryVersion==s.libraryVersion && f.moduleVersion==s.moduleVersion && f.type==s.kind){
                ++count;assert(!f.name.empty() && f.behavior!=OrbisV4DependencyDiagnostic::Behavior::Unknown);
            }
            assert(count==1);
        }
    }
    assert(OrbisV4ProviderFunctions::get("user.prx").empty());
    std::cout<<"PASS every generated export has exactly one qualified behavior declaration; unknown PRX have no automatic contracts\n";
}
