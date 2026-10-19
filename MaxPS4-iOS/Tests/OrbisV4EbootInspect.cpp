// Read-only CLI: the production preflight, never an engine or CPU dispatch.
#include "../Native/OrbisV4Eboot.hpp"
#include "../Native/OrbisV4ExperimentalSELF.hpp"
#include "../Native/OrbisV4SystemInventory.hpp"
#include "../Native/OrbisV4TLSRelocations.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    if (argc<2) return 2;
    const bool experimental=std::string(argv[1])=="--experimental";
    bool appContent=false,audioOut=false,commonDialog=false,errorDialog=false,gnmDriver=false,inventory=false,npCommon=false,npManager=false,libcInternal=false,npTrophy=false,localServices=false,tlsPlan=false,tlsRuntime=false;
    int firstFile=experimental ? 2 : 1;
    while(experimental && firstFile<argc) {
        const std::string flag=argv[firstFile];
        if(flag=="--app-content" && !appContent)appContent=true;
        else if(flag=="--audio-out" && !audioOut)audioOut=true;
        else if(flag=="--common-dialog" && !commonDialog)commonDialog=true;
        else if(flag=="--error-dialog" && !errorDialog)errorDialog=true;
        else if(flag=="--gnm-driver" && !gnmDriver)gnmDriver=true;
        else if(flag=="--tls-runtime" && !tlsRuntime)tlsRuntime=true;
        else if(flag=="--tls-plan" && !tlsPlan)tlsPlan=true;
        else if(flag=="--inventory" && !inventory)inventory=true;
        else if(flag=="--np-common" && !npCommon)npCommon=true;
        else if(flag=="--np-manager" && !npManager)npManager=true;
        else if(flag=="--libc-internal" && !libcInternal)libcInternal=true;
        else if(flag=="--local-services" && !localServices)localServices=true;
        else if(flag=="--np-trophy" && !npTrophy)npTrophy=true;
        else break;
        ++firstFile;
    }
    if(firstFile>=argc || argc-firstFile>128)return 2;
    std::vector<std::vector<uint8_t>> snapshots;
    std::vector<maxps4::OrbisV4ExperimentalSELF::Input> inputs;
    for (int i=firstFile;i<argc;++i) {
        std::ifstream file(argv[i],std::ios::binary|std::ios::ate);
        if (!file) return 2;
        const auto size=file.tellg();
        if (size<64 || size>static_cast<std::streamoff>(maxps4::OrbisV4Eboot::file_limit)) return 2;
        file.seekg(0);
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(bytes.data()),size)) return 2;
        if (experimental) {
            maxps4::PS4ExecutableImage checked;
            const int inspected=checked.load(bytes.data(),bytes.size(),maxps4::PS4ExecutableImage::SelfPolicy::StageSignedClear);
            const uint64_t bias=(!inspected && checked.type==0xfe00) ? 0 : uint64_t(inputs.size()+1)*0x400000;
            if(i==firstFile && !inspected && !inventory)
                std::cout<<"root_candidate_entry="<<checked.entry<<" diagnostic_bias="<<bias
                         <<"; entry_transfer_attempted=false; backend=none; JIT_initialization_attempted=false\n";
            snapshots.push_back(std::move(bytes));
            inputs.push_back({std::filesystem::path(argv[i]).filename().string(),snapshots.back().data(),snapshots.back().size(),bias});
            continue;
        }
        std::string report;
        const int status=maxps4::OrbisV4Eboot::preflight(bytes.data(),bytes.size(),report);
        std::cout<<"file="<<argv[i]<<"\nproduction_preflight="<<status<<"\n"<<report;
    }
    if (experimental && tlsPlan) {
        using TLS=maxps4::OrbisV4TLSRelocations;
        std::vector<TLS::Input> files;
        for(size_t i=0;i<inputs.size();++i)files.push_back({inputs[i].name,inputs[i].bytes,inputs[i].size,uint32_t(i+1)});
        TLS::Plan plan;std::string reason;
        const int status=TLS::prepare(files,plan,reason);
        std::cout<<"tls_preparation="<<status<<"\n"<<reason<<"\n";
        if(status)return 2;
        for(const auto& module:plan.modules)for(const auto& patch:module.patches)
            std::cout<<"tls_patch module="<<module.name<<" target="<<patch.target<<" value="<<patch.module<<"\n";
        for(const auto& placement:plan.placements)
            std::cout<<"tls_layout id="<<placement.id<<" offset="<<placement.offset<<" size="<<placement.size<<"\n";
        // No staged file is written and no patched file is passed to the linker.
        // The normal preparation below must still reject unsupported process TLS.
    }
    if (experimental && inventory) {
        std::string report;size_t budget=32*1024*1024;
        for(const auto& in:inputs){
            std::vector<uint8_t> elf;std::string reason;
            if(maxps4::OrbisClearImage::normalize(in.bytes,in.size,elf,reason)){
                std::cerr<<reason<<'\n';return 2;
            }
            if(elf.size()>budget)return 2;
            budget-=elf.size();maxps4::OrbisMetadata metadata;
            if(!metadata.parse(elf.data(),elf.size())){std::cerr<<metadata.error<<'\n';return 2;}
            report+=maxps4::OrbisV4SystemInventory::json(metadata,in.name);
        }
        std::cout<<report;return 0;
    }
    if (experimental) {
        std::string report; std::unique_ptr<maxps4::OrbisV4Translation> owner;
        std::array<int32_t,4> parameters{};
        if (appContent) {
            const auto path=std::filesystem::path(argv[firstFile]).parent_path()/"sce_sys"/"param.sfo";
            std::ifstream file(path,std::ios::binary|std::ios::ate);
            if (!file) {std::cerr<<"APP.CONTENT: missing param.sfo: "<<path<<'\n';return 2;}
            const auto length=file.tellg();
            if(length<20 || length>1024*1024)return 2;
            file.seekg(0);std::vector<uint8_t> sfo(static_cast<size_t>(length));
            if(!file.read(reinterpret_cast<char*>(sfo.data()),length) ||
                !maxps4::OrbisV4AppContent::parameters_from_sfo(sfo,parameters,report)) {
                std::cerr<<"APP.CONTENT: invalid SFO: "<<report<<'\n';return 2;
            }
        }
        std::unique_ptr<maxps4::OrbisV4TLSRuntime> runtime;
        const int status=maxps4::OrbisV4ExperimentalSELF::prepare(inputs,[]{},owner,report,appContent,parameters,audioOut,commonDialog,errorDialog,gnmDriver,npCommon,npManager,libcInternal,npTrophy,localServices,tlsRuntime ? &runtime : nullptr);
        std::cout<<"experimental_preparation="<<status<<"\n"<<report;
        if(runtime) {
            auto primary=runtime->primary();
            {auto lease=runtime->lease(primary);std::cout<<"tls_main_fs="<<lease.fs_base()<<" tls_main_rsp="<<lease.rsp()<<"; CPU_executed=false\n";}
            maxps4::OrbisV4TLSRuntime::Thread second;
            const int initialized=runtime->create_thread(second);
            std::cout<<"tls_secondary_initialization="<<initialized<<"\n";
            if(!initialized)runtime->release_thread(second);
            runtime->close();std::cout<<"TLS_mappings_retired=true; CPU_executed=false\n";
        }
        if (owner) {owner->close(); std::cout<<"mappings_retired=true; CPU_executed=false\n";}
    }
}
