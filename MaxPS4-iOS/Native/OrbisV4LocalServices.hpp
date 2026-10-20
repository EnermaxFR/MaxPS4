#pragma once
#include "OrbisV4VirtualPRX.hpp"
#include "OrbisV4PngDec.hpp"
namespace maxps4 {
// Guest x86-64 providers, without host pointers, IO, accounts or device handles.
// Software lifetime is independent from device availability. Each rejected
// operation leaves caller-owned output bytes untouched.
struct OrbisV4LocalServices {
    struct Function {const char* nid;const char* name;bool implemented;};
    struct Module {const char* name;uint16_t version;std::vector<Function> functions;const char* file=nullptr; // file: when not name.prx
        std::string path() const {return file ? std::string(file) : std::string(name)+".prx";}};
    static std::vector<Module> modules(){return {
        {"libScePad",0x101,{{"6ncge5+l5Qs","scePadClose",false},{"gjP9-KQzoUk","scePadGetControllerInformation",false},{"hv1luiJrqQM","scePadInit",true},{"xk0AcarP3V4","scePadOpen",false},{"YndgXqQVV7c","scePadReadState",false}}},
        {"libSceUserService",0x101,{{"CdWp0oHWGr0","sceUserServiceGetInitialUser",false},{"j3YMu1MVNNo","sceUserServiceInitialize",true},{"bwFjS+bX9mA","sceUserServiceTerminate",true}}},
        {"libSceSysmodule",0x101,{{"g8cM39EUZ6o","sceSysmoduleLoadModule",false},{"eR2bZFAAU0Q","sceSysmoduleUnloadModule",false}}},
        {"libSceSaveDataDialog",0x101,{{"yEiJ-qqr6Cg","sceSaveDataDialogGetResult",false},{"s9e3+YpRnzw","sceSaveDataDialogInitialize",false},{"4tPhsP6FpDI","sceSaveDataDialogOpen",false},{"YuH2FA7azqQ","sceSaveDataDialogTerminate",false},{"KK3Bdg1RWK0","sceSaveDataDialogUpdateStatus",true}}},
        {"libSceVideoOut",0,{{"uquVH4-Du78","sceVideoOutClose",false},{"Up36PTk687E","sceVideoOutOpen",false},{"w3BY+tAEiQY","sceVideoOutRegisterBuffers",false},{"i6-sR91Wt-4","sceVideoOutSetBufferAttribute",true},{"CBiu4mCE1DA","sceVideoOutSetFlipRate",false}}},
        {"libSceDiscMap",0x101,{{"lbQKqsERhtE","sceDiscMapIsRequestOnHDD",false}}},
        {"libSceSaveData",0x101,{{"RQOqDbk3bSU","sceSaveDataCheckBackupData",false},{"S1GkePI17zQ","sceSaveDataDelete",false},{"65VH0Qaaz6s","sceSaveDataGetMountInfo",false},{"TywrFKCoLGY","sceSaveDataInitialize3",false},{"0z45PIH+SNI","sceSaveDataMount2",false},{"lU9YRFsgwSU","sceSaveDataRestoreBackupData",false},{"c88Yy54Mx0w","sceSaveDataSaveIcon",false},{"85zul--eGXs","sceSaveDataSetParam",false},{"yKDy8S5yLA0","sceSaveDataTerminate",false},{"BMR4F-Uek3E","sceSaveDataUmount",false},{"VwadwBBBJ80","sceSaveDataUmountWithBackup",false},{"j8xKtiFj0SY","sceSaveDataGetEventResult",false},{"QwOO7vegnV8","sceSaveDataGetSaveDataMemory2",false},{"cduy9v4YmT4","sceSaveDataSetSaveDataMemory2",false},{"oQySEUfgXRA","sceSaveDataSetupSaveDataMemory2",false},{"wiT9jeC7xPw","sceSaveDataSyncSaveDataMemory",false}}},
        {"libSceSystemService",0x101,{{"Vo5V8KAwCmk","sceSystemServiceHideSplashScreen",false},{"fZo48un7LK4","sceSystemServiceParamGetInt",false}}},
        {"libScePngDec",0x101,{{"m0uW+8pFyaw","scePngDecCreate",false},{"WC216DD3El4","scePngDecDecode",false},{"U6h4e5JRPaQ","scePngDecParseHeader",true},{"-6srIGbLTIU","scePngDecQueryMemorySize",false}}},
        {"libkernel",0x101,{{"k+AXqu2-eBc","getpagesize",true},{"3PtV6p3QNX4","scePthreadEqual",true}}},
        // Sonic Mania (CUSA07023). Online rankings: an offline console (signed out).
        {"libSceNpScore",0x101,{{"GWnWQNXZH5M","sceNpScoreCreateNpTitleCtxA",false},{"gW8qyjYrUbk","sceNpScoreCreateRequest",false},
            {"dK8-SgYf6r4","sceNpScoreDeleteRequest",false},{"ANJssPz3mY0","sceNpScoreRecordScoreAsync",false},
            {"y5ja7WI05rs","sceNpScoreGetRankingByRangeAAsync",false},{"dRszNNyGWkw","sceNpScoreGetRankingByAccountIdAsync",false},
            {"m1DfNRstkSQ","sceNpScorePollAsync",false}},"libSceNpScoreRanking.prx"},
        // Current tick: the host clock in the process profile (OrbisV4SystemServices).
        {"libSceRtc",0x101,{{"18B2NS1y9UU","sceRtcGetCurrentTick",false}}},
        // No screenshot feature exists, so an overlay for it is never shown.
        {"libSceScreenShot",9,{{"73WQ4Jj0nJI","sceScreenShotSetOverlayImageWithOrigin",true}}},
        // Declared dependency whose functions the title never imports (PSN toolkit):
        // only module_start, which succeeds without doing anything.
        {"libSceNpToolkit2",0,{{"BaOKcng8g88","module_start",true}}},
    };}
    // Modules 0-9 keep their historical slots. libkernel (9) carries a 2 MiB
    // state (kernel-object arena, OrbisV4ProcessBatch::kernelState) up to
    // 0x1298000, so later modules start at 0x1300000, below AudioOut (0x1800000).
    static uint64_t bias(size_t index){return index<10 ? 0x1000000+index*0x10000 : 0x1300000+(index-10)*0x10000;}
    // process=true (mission 50): profile used by OrbisV4ProcessBatch, where every
    // import of the title is bound at load time. Sysmodule load/unload then
    // report the (already present) module as loaded, and the user service has
    // one local MaxPS4 user (id 1). The default profile is unchanged.
    static constexpr int32_t localUser = 1;
    static std::vector<uint8_t> provider(const Module& module,bool process=false){
        if(std::string(module.name)=="libScePngDec")return OrbisV4PngDec::provider();
        std::vector<uint8_t> code;std::map<uint32_t,std::vector<size_t>> errors;
        auto emit=[&](std::initializer_list<uint8_t> b){code.insert(code.end(),b);};
        auto imm=[&](uint32_t v){for(unsigned i=0;i<4;++i)code.push_back(uint8_t(v>>(8*i)));};
        auto result=[&](uint32_t v){emit({0xb8});imm(v);emit({0xc3});};
        auto error=[&](uint8_t cc,uint32_t v){emit({0x0f,cc});errors[v].push_back(code.size());imm(0);};
        auto rip=[&](std::initializer_list<uint8_t> b){emit(b);imm(uint32_t(0x8000-(0x4000+code.size()+4)));};
        auto state=[&](uint32_t absent){rip({0x83,0x3d}); // cmp DWORD PTR [rip+disp],1 includes final immediate
            // rip() above calculated the end before the immediate; correct displacement.
            const size_t p=code.size()-4;const uint32_t d=uint32_t(0x8000-(0x4000+code.size()+1));
            for(unsigned i=0;i<4;++i)code[p+i]=uint8_t(d>>(8*i));
            emit({1});error(0x85,absent);};
        auto initialize=[&](uint32_t duplicate){emit({0x31,0xc0,0xba,1,0,0,0});rip({0xf0,0x0f,0xb1,0x15});
            error(0x85,duplicate);result(0);};
        auto terminate=[&](uint32_t absent){emit({0xb8,1,0,0,0,0x31,0xd2});rip({0xf0,0x0f,0xb1,0x15});
            error(0x85,absent);result(0);};
        std::vector<OrbisV4VirtualPRX::Export> exports;
        for(const auto& f:module.functions){const size_t start=code.size();const std::string n=f.name;
            if(n=="getpagesize")result(16384);
            else if(n=="scePthreadEqual"){
                // Opaque 64-bit thread handles: equality, no host dereference,
                // scheduler operation or fabricated successful thread creation.
                emit({0x31,0xc0,0x48,0x39,0xf7,0x0f,0x94,0xc0,0xc3});
            }
            else if(n=="scePadInit"){
                // Pad's idempotent software initialization allocates no device.
                emit({0xb8,1,0,0,0});rip({0x87,0x05});result(0);
            }else if(n=="scePadOpen"){
                state(0x80920005);emit({0x85,0xff});error(0x88,0x809b0001);result(0x80920007);
            }else if(n=="scePadClose" || n=="scePadReadState" || n=="scePadGetControllerInformation")result(0x80920003);
            else if(n=="sceUserServiceInitialize"){
                // No configurable scheduler exists. Null selects the local default;
                // explicit priorities are unsupported and never dereferenced.
                emit({0x48,0x85,0xff});error(0x85,0x80960006);initialize(0x80960003);
            }else if(n=="sceUserServiceTerminate")terminate(0x80960002);
            else if(n=="sceUserServiceGetInitialUser"){
                state(0x80960002);emit({0x48,0x85,0xff});error(0x84,0x80960005);
                if(process){emit({0xc7,0x07});imm(uint32_t(localUser));result(0);} // *user = local user
                else result(0x80960009);
            }else if(n=="sceSysmoduleLoadModule" || n=="sceSysmoduleUnloadModule"){
                // Public ABI is uint16_t; upper register bits are irrelevant.
                emit({0x66,0x85,0xff});error(0x84,0x805a1000);result(process ? 0 : 0x805a1001);
            }else if(n=="sceSaveDataDialogInitialize")result(0x80b8000f); // no shared dialog/UI manager
            
            else if(n=="sceSaveDataDialogTerminate")result(0x80b80003);
            else if(n=="sceSaveDataDialogUpdateStatus"){rip({0x8b,0x05});emit({0xc3});}
            else if(n=="sceSaveDataDialogOpen"){
                state(0x80b80006);emit({0x48,0x85,0xff});error(0x84,0x80b8000d);result(0x80b8000f);
            }else if(n=="sceSaveDataDialogGetResult")result(0x80b80005);
            else if(n=="sceVideoOutSetBufferAttribute"){
                // Void CPU structure builder: stage all inputs, probe all 40
                // writable bytes before any nonzero mutation. Seventh SysV arg
                // is pitch on the guest stack; never read the host stack.
                emit({0x44,0x8b,0x54,0x24,8,0x48,0x89,0xf8,0x4c,0x8d,0x5f,40});
                emit({0xf0,0x80,0x08,0,0x48,0xff,0xc0,0x4c,0x39,0xd8,0x75,0xf4});
                emit({0x89,0x37,0x89,0x57,4,0x89,0x4f,8,0x44,0x89,0x47,12,0x44,0x89,0x4f,16,
                      0x44,0x89,0x57,20,0x48,0xc7,0x47,24,0,0,0,0,0x48,0xc7,0x47,32,0,0,0,0,0xc3});
            }else if(n=="sceVideoOutOpen")result(0x80290018);
            else if(n.find("sceVideoOut")==0)result(0x8029000b);
            else if(n.find("sceSaveData")==0)result(0x809f0001);
            else if(n=="sceDiscMapIsRequestOnHDD")result(0x81100004);
            else if(n.find("sceSystemService")==0)result(0x8002002d); // kernel ENOTSUP; no console preferences/UI
            else if(n.find("sceNpScore")==0)result(0x80550006);       // SCE_NP_ERROR_SIGNED_OUT: no PSN, no rankings
            else if(n=="sceRtcGetCurrentTick")result(0x8002002d);      // without the host clock (default profile)
            else if(n=="sceScreenShotSetOverlayImageWithOrigin" || n=="module_start")result(0);
            else throw std::invalid_argument("unreviewed local service");
            exports.push_back({f.nid,start,code.size()-start});
        }
        for(const auto& e:errors){for(auto p:e.second){const auto d=uint32_t(code.size()-p-4);
            for(unsigned i=0;i<4;++i)code[p+i]=uint8_t(d>>(8*i));}result(e.first);}
        return OrbisV4VirtualPRX::build(module.name,code,4,exports,module.version);
    }
};
}
