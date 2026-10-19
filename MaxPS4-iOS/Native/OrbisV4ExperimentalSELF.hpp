#pragma once
#include <cstdio>
#include "OrbisV4Eboot.hpp"
#include "OrbisV4TLSRuntime.hpp"
#include "OrbisV4AppContent.hpp"
#include "OrbisV4AudioOut.hpp"
#include "OrbisV4CommonDialog.hpp"
#include "OrbisV4ErrorDialog.hpp"
#include "OrbisV4GnmDriver.hpp"
#include "OrbisV4NpCommon.hpp"
#include "OrbisV4DependencyDiagnostic.hpp"
#include "OrbisV4ProviderFunctions.hpp"

namespace maxps4 {
// Explicit experimental policy: signature is not an admission requirement;
// encryption/compression, unknown profiles and unbacked bytes remain refused.
// This does not verify authenticity, invent providers or broaden CPU admission.
struct OrbisV4ExperimentalSELF {
    static int normalize(const uint8_t* bytes,size_t size,std::vector<uint8_t>& output,std::string& reason) {
        // Preserve this SELF-specific API; mixed batches use OrbisClearImage.
        if (bytes && size>=4 && OrbisMetadata::read(bytes,4)!=0x1d3d154f) {
            output.clear(); reason="SELF.EXPERIMENTAL: SELF required"; return -ENOTSUP;
        }
        return OrbisClearImage::normalize(bytes,size,output,reason);
    }
    struct Input {std::string name; const uint8_t* bytes; size_t size; uint64_t bias;};
    static int prepare(const std::vector<Input>& inputs,OrbisV4Translation::Invalidator invalidate,
                       std::unique_ptr<OrbisV4Translation>& output,std::string& report,bool appContent=false,
                       const std::array<int32_t,4>& appParameters={},bool audioOut=false,bool commonDialog=false,bool errorDialog=false,bool gnmDriver=false,bool npCommon=false,bool npManager=false,bool libcInternal=false,bool npTrophy=false,bool localServices=false,std::unique_ptr<OrbisV4TLSRuntime>* tlsRuntime=nullptr) {
        output.reset(); report.clear();
        if (inputs.empty() || inputs.size()>128U-size_t(appContent)-size_t(audioOut)-size_t(commonDialog)-size_t(errorDialog)-size_t(gnmDriver)-size_t(npCommon)-size_t(npManager)-size_t(libcInternal)-size_t(npTrophy)-(localServices ? OrbisV4LocalServices::modules().size() : 0)) {report="SELF.EXPERIMENTAL: invalid module count"; return -EINVAL;}
        std::vector<std::vector<uint8_t>> normalized(inputs.size());
        std::vector<OrbisLinker::Input> linkInputs;
        std::vector<uint8_t> appContentBytes;
        std::vector<uint8_t> audioOutBytes;
        std::vector<uint8_t> commonDialogBytes;
        std::vector<uint8_t> errorDialogBytes;
        std::vector<uint8_t> gnmDriverBytes;
        std::vector<uint8_t> npCommonBytes,npManagerBytes,libcInternalBytes,npTrophyBytes;
        if (appContent) {
            if (std::any_of(inputs.begin(),inputs.end(),[](const auto& in){return in.name==OrbisV4AppContent::path;})) {
                report="APP.CONTENT: explicit module conflicts with virtual provider\n";return -EINVAL;
            }
            appContentBytes=OrbisV4AppContent::provider(appParameters);
            report="APP.CONTENT: explicit virtual provider; Initialize/GetInt(1..4); SKU unsupported; no Sony PRX or entitlement\n";
            for(size_t i=0;i<4;++i)report+="USER_DEFINED_PARAM_"+std::to_string(i+1)+"="+std::to_string(appParameters[i])+"\n";
        }
        if (audioOut) {
            if (std::any_of(inputs.begin(),inputs.end(),[](const auto& in){return in.name==OrbisV4AudioOut::path;})) {
                report+="AUDIO.OUT: explicit module conflicts with virtual provider\n";return -EINVAL;
            }
            audioOutBytes=OrbisV4AudioOut::provider();
            report+="AUDIO.OUT: explicit virtual provider; software init; backend unavailable; no handles or PCM transfers\n";
        }
        if (commonDialog) {
            if (std::any_of(inputs.begin(),inputs.end(),[](const auto& in){return in.name==OrbisV4CommonDialog::path;})) {
                report+="COMMON.DIALOG: explicit module conflicts with virtual provider\n";return -EINVAL;
            }
            commonDialogBytes=OrbisV4CommonDialog::provider();
            report+="COMMON.DIALOG: explicit software Initialize only; no UI opened or answered\n";
        }
        if (errorDialog) {
            if (std::any_of(inputs.begin(),inputs.end(),[](const auto& in){return in.name==OrbisV4ErrorDialog::path;})) {
                report+="ERROR.DIALOG: explicit module conflicts with virtual provider\n";return -EINVAL;
            }
            errorDialogBytes=OrbisV4ErrorDialog::provider();
            report+="ERROR.DIALOG: software lifetime; Open returns NOT_SUPPORTED; no UI or fabricated completion\n";
        }
        if (gnmDriver) {
            if (std::any_of(inputs.begin(),inputs.end(),[](const auto& in){return in.name==OrbisV4GnmDriver::path;})) {
                report+="GNM.DRIVER: explicit module conflicts with virtual provider\n";return -EINVAL;
            }
            gnmDriverBytes=OrbisV4GnmDriver::provider();
            report+="GNM.DRIVER: partial provider; CPU DrawIndexAuto packet only; no GPU submissions, queues, shaders or flips; other imports unresolved\n";
        }
        if (npCommon) {
            if (std::any_of(inputs.begin(),inputs.end(),[](const auto& in){return in.name==OrbisV4NpCommon::path;})) {
                report+="NP.COMMON: explicit module conflicts with virtual provider\n";return -EINVAL;
            }
            npCommonBytes=OrbisV4NpCommon::provider();
            report+="NP.COMMON: partial offline identifier comparisons only; no PSN session, accounts or network\n";
        }
        if (npManager) {
            if (std::any_of(inputs.begin(),inputs.end(),[](const auto& in){return in.name==OrbisV4NpManager::path;})) {
                report+="NP.MANAGER: explicit module conflicts with virtual provider\n";return -EINVAL;
            }
            npManagerBytes=OrbisV4NpManager::provider();
            report+="NP.MANAGER: offline SignedOut query; ID queries fail; no init, account or PSN connection\n";
        }
        if (libcInternal) {
            if (std::any_of(inputs.begin(),inputs.end(),[](const auto& in){return in.name==OrbisV4LibcInternal::path;})) {
                report+="LIBC.INTERNAL: explicit module conflicts with virtual provider\n";return -EINVAL;
            }
            libcInternalBytes=OrbisV4LibcInternal::provider();
            report+="LIBC.INTERNAL: memcpy/memset byte operations only; no heap, IO, time or Ext exports\n";
        }
        if (npTrophy) {
            if (std::any_of(inputs.begin(),inputs.end(),[](const auto& in){return in.name==OrbisV4NpTrophy::path;})) {
                report+="NP.TROPHY: explicit module conflicts with virtual provider\n";return -EINVAL;
            }
            npTrophyBytes=OrbisV4NpTrophy::provider();
            report+="NP.TROPHY: explicit refusals only; no contexts, handles, trophies, accounts, storage or PSN sync\n";
        }
        const auto localModules=localServices ? OrbisV4LocalServices::modules() : std::vector<OrbisV4LocalServices::Module>{};
        std::vector<std::vector<uint8_t>> localBytes;
        for(const auto& module:localModules){
            if(std::any_of(inputs.begin(),inputs.end(),[&](const auto& in){return in.name==module.path();})){
                report+="LOCAL.SERVICES: explicit module conflicts with virtual provider\n";return -EINVAL;
            }
            localBytes.push_back(OrbisV4LocalServices::provider(module));
            report+="LOCAL.SERVICES: partial provider="+module.path()+"; software lifetime/CPU structures only; unavailable devices/IO/UI refused\n";
        }
        size_t normalizedBudget=appContentBytes.size()+audioOutBytes.size()+commonDialogBytes.size()+errorDialogBytes.size()+gnmDriverBytes.size()+npCommonBytes.size()+npManagerBytes.size()+libcInternalBytes.size()+npTrophyBytes.size();
        for(const auto& bytes:localBytes)normalizedBudget+=bytes.size();
        for (size_t i=0;i<inputs.size();++i) {
            std::string reason;
            const auto& in=inputs[i];
            int status=OrbisClearImage::normalize(in.bytes,in.size,normalized[i],reason);
            report+=in.name+": "+reason+"\n";
            if (status) return status;
            if (normalized[i].size()>32*1024*1024-normalizedBudget) {report+="ORBIS.BUDGET: normalized batch exceeds 32 MiB\n";return -ENOMEM;}
            normalizedBudget+=normalized[i].size();
            OrbisMetadata metadata;
            if (!metadata.parse(normalized[i].data(),normalized[i].size())) {
                report+="ORBIS.METADATA: "+metadata.error+"\n";return -ENOTSUP;
            }
            if (metadata.type==0xfe00 && in.bias) {report+="ORBIS.BIAS: static ET_SCE_EXEC requires zero bias\n";return -ENOTSUP;}
            const auto imports=std::count_if(metadata.symbols.begin(),metadata.symbols.end(),[](const auto& s) {
                return !s.defined && !s.encoded.empty();
            });
            report+="module="+in.name+" entry="+std::to_string(metadata.entry)+
                " symbol_entries="+std::to_string(metadata.symbols.size())+" imports="+std::to_string(imports)+
                " relocations="+std::to_string(metadata.relocations.size())+"\n";
            linkInputs.push_back({{{in.name,metadata.needed},normalized[i].data(),normalized[i].size()},in.bias});
        }
        if (appContent) linkInputs.push_back({{{OrbisV4AppContent::path,{}},appContentBytes.data(),appContentBytes.size()},OrbisV4AppContent::bias});
        if (audioOut) linkInputs.push_back({{{OrbisV4AudioOut::path,{}},audioOutBytes.data(),audioOutBytes.size()},OrbisV4AudioOut::bias});
        if (commonDialog) linkInputs.push_back({{{OrbisV4CommonDialog::path,{}},commonDialogBytes.data(),commonDialogBytes.size()},OrbisV4CommonDialog::bias});
        if (errorDialog) linkInputs.push_back({{{OrbisV4ErrorDialog::path,{}},errorDialogBytes.data(),errorDialogBytes.size()},OrbisV4ErrorDialog::bias});
        if (gnmDriver) linkInputs.push_back({{{OrbisV4GnmDriver::path,{}},gnmDriverBytes.data(),gnmDriverBytes.size()},OrbisV4GnmDriver::bias});
        if (npCommon) linkInputs.push_back({{{OrbisV4NpCommon::path,{}},npCommonBytes.data(),npCommonBytes.size()},OrbisV4NpCommon::bias});
        if (npManager) linkInputs.push_back({{{OrbisV4NpManager::path,{}},npManagerBytes.data(),npManagerBytes.size()},OrbisV4NpManager::bias});
        if (libcInternal) linkInputs.push_back({{{OrbisV4LibcInternal::path,{}},libcInternalBytes.data(),libcInternalBytes.size()},OrbisV4LibcInternal::bias});
        if (npTrophy) linkInputs.push_back({{{OrbisV4NpTrophy::path,{}},npTrophyBytes.data(),npTrophyBytes.size()},OrbisV4NpTrophy::bias});
        for(size_t i=0;i<localModules.size();++i)linkInputs.push_back({{{localModules[i].path(),{}},localBytes[i].data(),localBytes[i].size()},OrbisV4LocalServices::bias(i)});
        // Supplementary metadata diagnostics do not grant linker admission.
        try {
            using Audit=OrbisV4DependencyDiagnostic;
            std::vector<Audit::Input> auditInputs;
            for(size_t i=0;i<linkInputs.size();++i){const auto& file=linkInputs[i].file;
                auditInputs.push_back({file.request.logicalPath,file.bytes,file.size,
                    i<inputs.size() ? Audit::Coverage::Unknown : Audit::Coverage::Partial,
                    i<inputs.size() ? std::vector<Audit::Function>{} : OrbisV4ProviderFunctions::get(file.request.logicalPath)});}
            report+=Audit::inspect(std::move(auditInputs)).text;
        } catch(const std::exception& e) {
            report+="[ORBIS.GLOBAL] incomplete diagnostic: "+std::string(e.what())+"; admission=false\n";
        }
        if(tlsRuntime) {
            std::vector<uint32_t> ids;
            for(size_t i=0;i<linkInputs.size();++i) {
                ids.push_back(uint32_t(i+1));
                report+="TLS.REGISTRY: id="+std::to_string(i+1)+" module="+linkInputs[i].file.request.logicalPath+"\n";
            }
            OrbisLinker::TLSImage image;OrbisLinker::Report details;std::string error;
            if(!OrbisLinker::linkTLS(linkInputs,ids,{},image,error,32*1024*1024,&details)) {
                report+=error+"\n"+details.text();return -ENOTSUP;
            }
            for(const auto& m:image.image().modules) if(m.moduleParam.present) {
                char sdk[16];std::snprintf(sdk,sizeof(sdk),"0x%08x",unsigned(m.moduleParam.sdkVersion));
                report+="MODULE.PARAM: module="+m.path+" guest="+std::to_string(m.bias+m.moduleParam.address)+
                    " version=1 sdk="+sdk+" view=PT_LOAD mapping=none writes=none\n";
            }
            std::unique_ptr<OrbisV4TLSRuntime> pending;
            const int status=OrbisV4TLSRuntime::load(image,std::move(invalidate),pending,error);
            report+=error+"\n"+details.text()+"\n";
            if(status)return status;
            *tlsRuntime=std::move(pending);
            return 0;
        }
        OrbisLinker::Image image; OrbisLinker::Report details; std::string error;
        if (!OrbisLinker::link(linkInputs,{},image,error,32*1024*1024,&details)) {
            report+=error+"\n"+details.text();return -ENOTSUP;
        }
        OrbisPRXFEXBridge bridge;
        if (!bridge.map(image)) {report+="ORBIS.MAP: "+bridge.diagnostic()+"\n"; return -ENOTSUP;}
        output=bridge.make_translation(std::move(invalidate));
        report+="V4.MAPPED: translated lease owner; signature unverified; CPU_executed=false\n";
        return 0;
    }
    // canonical owns the request bytes until synchronous dispatch returns.
    static int admit(const uint8_t* bytes,size_t size,std::vector<uint8_t>& canonical,
                     maxps4_v4_prx_request& request,std::string& report) {
        request={}; canonical.clear();
        std::unique_ptr<OrbisV4Translation> owner;
        int status=prepare({{"eboot.bin",bytes,size,0}},[]{},owner,report);
        if (status) return status;
        owner->close(); owner.reset();
        std::string reason;
        status=normalize(bytes,size,canonical,reason);
        if (status) {report+=reason; return status;}
        PS4ExecutableImage image;
        if (image.load(canonical.data(),canonical.size()) || image.type!=0xfe00 ||
            image.requiresLinker || image.requiresTLS) {
            report+="EBOOT.ABI: dynamic process startup is not implemented; no CPU dispatch\n";
            canonical.clear();return -ENOTSUP;
        }
        request={canonical.data(),canonical.size(),0,image.entry,MAXPS4_V4_ABI_STARTUP_RETURN,64};
        status=OrbisV4PRXLaunch::prepare(request,[]{},owner,&reason);
        report+=reason+"\n";
        if (owner) owner->close();
        if (status) {request={};canonical.clear();}
        report+="CPU_executed=false; mappings_retired=true; experimental unauthenticated policy explicit\n";
        return status;
    }

};
}
