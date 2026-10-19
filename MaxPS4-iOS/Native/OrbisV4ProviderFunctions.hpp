#pragma once
#include "OrbisV4DependencyDiagnostic.hpp"
#include "OrbisV4AppContent.hpp"
#include "OrbisV4AudioOut.hpp"
#include "OrbisV4CommonDialog.hpp"
#include "OrbisV4ErrorDialog.hpp"
#include "OrbisV4GnmDriver.hpp"
#include "OrbisV4NpCommon.hpp"
#include "OrbisV4NpManager.hpp"
#include "OrbisV4NpTrophy.hpp"
#include "OrbisV4LibcInternal.hpp"
#include "OrbisV4LocalServices.hpp"
namespace maxps4 {
// Reviewed declarations for generated providers only. Do not assign these to
// arbitrary user PRX files by filename. Exact identity matching is still needed.
struct OrbisV4ProviderFunctions {
    static std::vector<OrbisV4DependencyDiagnostic::Function> get(const std::string& path){
        using Audit=OrbisV4DependencyDiagnostic;using B=Audit::Behavior;
        std::vector<Audit::Function> out;
        auto add=[&](const char* lib,const char* mod,const char* nid,const char* name,B behavior){
            out.push_back({nid,lib,mod,name,1,0x101,2,behavior});};
        if(path==OrbisV4AppContent::path){
            add("libSceAppContent","libSceAppContentUtil","R9lA82OraNs","sceAppContentInitialize",B::ImplementedProfile);
            add("libSceAppContent","libSceAppContentUtil","99b82IKXpH4","sceAppContentAppParamGetInt",B::PartialProfile);
        }else if(path==OrbisV4AudioOut::path){
            const char* names[]={"sceAudioOutClose","sceAudioOutInit","sceAudioOutOpen","sceAudioOutOutput","sceAudioOutSetVolume"};
            for(size_t i=0;i<5;++i)add("libSceAudioOut","libSceAudioOut",OrbisV4AudioOut::nids[i],names[i],i==1 ? B::ImplementedProfile : B::ExplicitRefusal);
        }else if(path==OrbisV4CommonDialog::path){
            add("libSceCommonDialog","libSceCommonDialog","uoUpLGNkygk","sceCommonDialogInitialize",B::ImplementedProfile);
        }else if(path==OrbisV4ErrorDialog::path){
            const char* names[]={"sceErrorDialogInitialize","sceErrorDialogOpen","sceErrorDialogTerminate","sceErrorDialogUpdateStatus"};
            for(size_t i=0;i<4;++i)add("libSceErrorDialog","libSceErrorDialog",OrbisV4ErrorDialog::nids[i],names[i],i==1 ? B::ExplicitRefusal : B::ImplementedProfile);
        }else if(path==OrbisV4GnmDriver::path){
            const char* names[]={"sceGnmDrawIndexAuto","sceGnmAreSubmitsAllowed","sceGnmDriverCaptureInProgress","sceGnmDriverTraceInProgress",
                "sceGnmIsUserPaEnabled","sceRazorIsLoaded","sceGnmValidateOnSubmitEnabled","sceGnmDriverTriggerCapture","sceGnmSubmitDone",
                "sceGnmSubmitCommandBuffers","sceGnmSubmitCommandBuffersForWorkload","sceGnmSubmitAndFlipCommandBuffers",
                "sceGnmSubmitAndFlipCommandBuffersForWorkload","sceGnmMapComputeQueue","sceGnmMapComputeQueueWithPriority","sceGnmUnmapComputeQueue"};
            for(size_t i=0;i<16;++i)add("libSceGnmDriver","libSceGnmDriver",OrbisV4GnmDriver::nids[i],names[i],
                i==0 ? B::PartialProfile : i<=6 ? B::ImplementedProfile : B::ExplicitRefusal);
        }else if(path==OrbisV4NpCommon::path){
            add("libSceNpCommon","libSceNpCommon",OrbisV4NpCommon::nids[0],"sceNpCmpOnlineId",B::ImplementedProfile);
            add("libSceNpCommon","libSceNpCommon",OrbisV4NpCommon::nids[1],"sceNpCmpNpId",B::ImplementedProfile);
        }else if(path==OrbisV4NpManager::path){
            const char* names[]={"sceNpGetState","sceNpGetNpId","sceNpGetOnlineId"};
            for(size_t i=0;i<3;++i)add("libSceNpManager","libSceNpManager",OrbisV4NpManager::nids[i],names[i],i ? B::ExplicitRefusal : B::ImplementedProfile);
        }else if(path==OrbisV4NpTrophy::path){
            const char* names[]={"sceNpTrophyCreateContext","sceNpTrophyDestroyContext","sceNpTrophyAbortHandle","sceNpTrophyCreateHandle",
                "sceNpTrophyDestroyHandle","sceNpTrophyGetTrophyUnlockState","sceNpTrophyRegisterContext","sceNpTrophyUnlockTrophy"};
            for(size_t i=0;i<8;++i)add("libSceNpTrophy","libSceNpTrophy",OrbisV4NpTrophy::nids[i],names[i],B::ExplicitRefusal);
        }else if(path==OrbisV4LibcInternal::path){
            add("libSceLibcInternal","libSceLibcInternal",OrbisV4LibcInternal::nids[0],"memset",B::ImplementedProfile);
            add("libSceLibcInternal","libSceLibcInternal",OrbisV4LibcInternal::nids[1],"memcpy",B::ImplementedProfile);
        }
        for(const auto& module:OrbisV4LocalServices::modules())if(path==module.path()){
            for(const auto& f:module.functions)out.push_back({f.nid,module.name,module.name,f.name,1,module.version,2,
                std::string(f.name)=="scePngDecParseHeader" ? B::PartialProfile : f.implemented ? B::ImplementedProfile : B::ExplicitRefusal});
        }
        return out;
    }
};
}
