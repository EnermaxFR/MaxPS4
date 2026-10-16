#include <FEXCore/Utils/LogManager.h>
#include "GuestELFFixture.h"
#include "OrbisM1Fixture.h"
#include "../Native/maxps4_fex_runtime.h"
#include "PS4LinkedELFFixture.h"
#include "PS4OrbisExecutionFixture.h"
#include "PS4PlayableFixture.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#if defined(__linux__)
#include <signal.h>
#include <ucontext.h>
#include <execinfo.h>
// CI-only fatal diagnostics. The application runtime installs no signal handler.
static void fault_diagnostic(int signal, siginfo_t* info, void* raw) {
    char line[160];
    auto emit = [&](const char* name, uint64_t value) {
        size_t n=0; while (*name) line[n++]=*name++;
        line[n++]='='; line[n++]='0'; line[n++]='x';
        for (int shift=60;shift>=0;shift-=4) line[n++]="0123456789abcdef"[(value>>shift)&15];
        line[n++]='\n'; (void)::write(STDERR_FILENO,line,n);
    };
    emit("FATAL signal",signal); emit("fault address",reinterpret_cast<uintptr_t>(info->si_addr));
    const auto* context=static_cast<const ucontext_t*>(raw);
#if defined(__aarch64__)
    emit("native PC",context->uc_mcontext.pc); emit("native SP",context->uc_mcontext.sp);
    for (unsigned i=0;i<31;++i) {
        char name[]="x00"; name[1]='0'+i/10; name[2]='0'+i%10;
        emit(name,context->uc_mcontext.regs[i]);
    }
    if (context->uc_mcontext.pc>=4096 && context->uc_mcontext.pc!=reinterpret_cast<uintptr_t>(info->si_addr)) {
        const auto* instruction=reinterpret_cast<const uint32_t*>(context->uc_mcontext.pc);
        emit("fault instruction",instruction[0]);
    }
#elif defined(__x86_64__) && defined(REG_RIP)
    emit("native PC",context->uc_mcontext.gregs[REG_RIP]);
#endif
    void* frames[24]; const int count=::backtrace(frames,24);
    ::backtrace_symbols_fd(frames,count,STDERR_FILENO);
    _Exit(128+signal);
}
#endif

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static uint64_t value(const uint8_t* bytes) {
    uint64_t result = 0;
    for (size_t i = 0; i < 8; ++i) result |= uint64_t(bytes[i]) << (8*i);
    return result;
}
static int playableTest(const char* evidence = nullptr) {
    using namespace playable_fixture;
    for (int restart=0;restart<2;++restart) {
        auto file=app(), provider=orbis_execution::make(true);
        if (restart) {
            const std::vector<uint8_t> pattern{0xb8,0x18,0x20,0x30,0xff};
            const auto at=std::search(file.begin()+0x1000,file.end(),pattern.begin(),pattern.end());
            CHECK(at!=file.end()); *(at+1)=0x19; // Change actual guest pixel instruction.
        }
        const char* deps[]={"provider.prx"};
        const maxps4_orbis_module modules[]={
            {"app.elf",file.data(),file.size(),orbis_execution::app_bias,deps,1},
            {"provider.prx",provider.data(),provider.size(),orbis_execution::provider_bias,nullptr,0}};
        maxps4_fex_engine* process=nullptr;
        CHECK(maxps4_fex_engine_open(&process)==0);
        struct Close { maxps4_fex_engine* p; ~Close(){ maxps4_fex_engine_close(p); } } close{process};
        char reason[1024]{};
        CHECK(maxps4_fex_engine_prepare_orbis(process,modules,2,"app.elf",65536,reason,sizeof(reason))==0);
        CHECK(maxps4_fex_engine_configure_frame(process,pixels,input,width,height)==0);
        std::vector<uint8_t> out(width*height*4,0xa5), memory(out.size());
        uint64_t sequence=99; uint32_t backend=99;
        CHECK(maxps4_fex_engine_step_frame(process,0,out.data(),out.size(),&sequence,&backend)==-EAGAIN);
        CHECK(!sequence && !backend && out[0]==0xa5);
        CHECK(maxps4_fex_engine_initialize(process)==0);
        CHECK(maxps4_fex_engine_configure_frame(process,pixels,input,0,height)==-EINVAL);
        CHECK(maxps4_fex_engine_step_frame(process,0,out.data(),out.size(),&sequence,&backend)!=0 && out[0]==0xa5);
        CHECK(maxps4_fex_engine_configure_frame(process,UINT64_MAX-3,input,width,height)==-EFAULT);
        CHECK(maxps4_fex_engine_configure_frame(process,pixels,pixels,width,height)==-EINVAL);
        CHECK(maxps4_fex_engine_configure_frame(process,pixels,input,width,height)==0);
        CHECK(maxps4_fex_engine_step_frame(process,32,out.data(),out.size(),&sequence,&backend)==-EINVAL);
        CHECK(maxps4_fex_engine_step_frame(process,0,out.data(),out.size()-1,&sequence,&backend)==-EINVAL);
        CHECK(out[0]==0xa5);
        uint32_t expectedX=7,expectedY=3; uint64_t expectedSequence=0;
        for (uint32_t buttons : {0u,2u,1u,8u,4u,16u,3u,12u,2u,2u,2u,2u}) {
            expectedX=(expectedX+((buttons>>1)&1)-(buttons&1))&15;
            expectedY=(expectedY+((buttons>>3)&1)-((buttons>>2)&1))&7;
            CHECK(maxps4_fex_engine_step_frame(process,buttons,out.data(),out.size(),&sequence,&backend)==0);
            CHECK(sequence==++expectedSequence);
#ifdef VIXL_SIMULATOR
            CHECK(backend==MAXPS4_CPU_FEX_ARM64_SIMULATOR);
#else
            CHECK(backend==MAXPS4_CPU_FEX_ARM64_JIT);
#endif
            CHECK(maxps4_fex_engine_read_ps4(process,pixels,memory.data(),memory.size())==0 && memory==out);
            uint8_t state[16]{};
            CHECK(maxps4_fex_engine_read_ps4(process,input,state,sizeof(state))==0);
            CHECK(state[0]==buttons && state[4]==expectedX && state[8]==expectedY && state[12]==sequence);
            for (uint32_t py=0;py<height;++py) for (uint32_t px=0;px<width;++px) {
                const size_t off=(py*width+px)*4;
                uint32_t pixel=0; for (int b=0;b<4;++b) pixel|=uint32_t(out[off+b])<<(8*b);
                const uint32_t expected = py<16 && px/2==expectedX && py/2==expectedY ?
                    (0xff2080e0 | ((buttons&16)<<16)) : 0xff302018;
                CHECK(pixel==((restart && px==0 && py==0 && expected==0xff302018) ? 0xff302019 : expected));
            }
            uint64_t hash=14695981039346656037ull;
            for (auto byte:out) hash=(hash^byte)*1099511628211ull;
            std::printf("PASS guest-frame restart=%d sequence=%llu backend=%u input=%u x=%u y=%u bytes=%zu FNV64=%llx guest-memory-equal=yes\n",
                restart,(unsigned long long)sequence,backend,buttons,expectedX,expectedY,out.size(),(unsigned long long)hash);
            if (evidence && !restart) {
                std::filesystem::create_directories(evidence);
                std::ofstream rgba(std::filesystem::path(evidence)/("frame-"+std::to_string(sequence)+".rgba"),std::ios::binary);
                rgba.write(reinterpret_cast<const char*>(out.data()),out.size()); CHECK(bool(rgba));
                std::ofstream ppm(std::filesystem::path(evidence)/("frame-"+std::to_string(sequence)+".ppm"),std::ios::binary);
                ppm << "P6\n32 24\n255\n";
                for (size_t off=0;off<out.size();off+=4) ppm.write(reinterpret_cast<const char*>(out.data()+off),3);
                CHECK(bool(ppm));
            }
        }
        CHECK(maxps4_fex_engine_prepare_orbis(process,modules,1,"app.elf",65536,reason,sizeof(reason))!=0);
        CHECK(maxps4_fex_engine_step_frame(process,0,out.data(),out.size(),&sequence,&backend)==-EFAULT);
        CHECK(!sequence && !backend);
    }
    std::puts("PASS real FEX framebuffer, input-to-guest-state, successive images, action, machine-code mutation and close/restart");
    return 0;
}
static int animatedGuestTest() {
    using namespace playable_fixture;
    auto file=app(true), provider=orbis_execution::make(true);
    const char* deps[]={"provider.prx"};
    const maxps4_orbis_module modules[]={
        {"app.elf",file.data(),file.size(),orbis_execution::app_bias,deps,1},
        {"provider.prx",provider.data(),provider.size(),orbis_execution::provider_bias,nullptr,0}};
    maxps4_fex_engine* engine=nullptr;
    CHECK(maxps4_fex_engine_open(&engine)==0);
    struct Close { maxps4_fex_engine* p; ~Close(){maxps4_fex_engine_close(p);} } close{engine};
    char reason[1024]{};
    const int prepared=maxps4_fex_engine_prepare_orbis(engine,modules,2,"app.elf",65536,reason,sizeof(reason));
    if (prepared) std::fprintf(stderr,"animated guest prepare failed: status=%d reason=%s ELF_bytes=%zu\\n",prepared,reason,file.size());
    CHECK(prepared==0);
    CHECK(maxps4_fex_engine_configure_frame(engine,pixels,input,width,height)==0);
    CHECK(maxps4_fex_engine_initialize(engine)==0);
    std::vector<uint8_t> image(width*height*4);
    uint64_t sequence=0; uint32_t backend=0;
    for (uint32_t frame=0;frame<20;++frame) {
        CHECK(maxps4_fex_engine_step_frame(engine,frame<2 ? 8u : (frame==9 ? 16u : 0u),image.data(),image.size(),&sequence,&backend)==0);
        CHECK(sequence==frame+1);
#ifdef VIXL_SIMULATOR
        CHECK(backend==MAXPS4_CPU_FEX_ARM64_SIMULATOR);
#else
        CHECK(backend==MAXPS4_CPU_FEX_ARM64_JIT);
#endif
        const uint32_t enemyCell=80+(frame&15);
        const size_t enemyOffset=((enemyCell/16)*2*width+(enemyCell%16)*2)*4;
        uint32_t rgba=0;
        for(int b=0;b<4;++b) rgba|=uint32_t(image[enemyOffset+b])<<(8*b);
        CHECK(rgba==(frame==7 ? 0xff2080e0u : 0xff40d030u));
        if (frame==9) {
            const size_t playerOffset=((5u*2u)*width+(7u*2u))*4u;
            uint32_t player=0;
            for (int b=0;b<4;++b) player|=uint32_t(image[playerOffset+b])<<(8*b);
            CHECK(player==0xffa080e0u); // Action visibly changes red channel in guest code.
        }
        if(frame>0) {
            const uint32_t oldCell=80+((frame-1)&15);
            if(oldCell!=enemyCell) {
                const size_t oldOffset=((oldCell/16)*2*width+(oldCell%16)*2)*4;
                uint32_t oldRGBA=0;
                for(int b=0;b<4;++b) oldRGBA|=uint32_t(image[oldOffset+b])<<(8*b);
                CHECK(oldRGBA==(oldCell==87 ? 0xff2080e0u : 0xff302018u));
            }
        }
    }
    std::puts("PASS guest x86-64 autonomous sprite animation and collision across 20 real FEX frames");
    return 0;
}
static int m1Test() {
    using namespace orbis_m1_fixture;
    maxps4_fex_engine* engine = nullptr;
    CHECK(maxps4_fex_engine_open(&engine) == 0);
    struct CloseM1 { maxps4_fex_engine* engine; ~CloseM1() { maxps4_fex_engine_close(engine); } } close{engine};
    maxps4_m1_report report{};
    auto file = make();
    CHECK(maxps4_fex_engine_run_m1(engine,file.data(),file.size(),100,snapshot,&report) == -EAGAIN);
    CHECK(maxps4_fex_engine_initialize(engine) == 0);
    for (uint32_t initial : {17U,83U,129U}) for (bool calls : {false,true}) {
        file = make(initial,calls);
        for (int run=0;run<2;++run) {
            const int32_t status = maxps4_fex_engine_run_m1(engine,file.data(),file.size(),100,snapshot,&report);
            if (status) std::fprintf(stderr,"M1 run rejected: status=%d initial=%u calls=%d\n",status,initial,calls);
            CHECK(status == 0);
            CHECK(report.rax == initial+12 && report.memory == initial+12 && report.tls_word == initial+5);
            CHECK(report.heap_word == initial+5 && report.zero_memory == 0 && report.kernel_return == (calls ? 1U : 0U));
            CHECK(report.entry == entry && report.calls == (calls ? 2U : 0U));
            CHECK(!(report.rsp & 15) && report.fs_base == maxps4::OrbisM1::thread_base+0x20010);
#ifdef VIXL_SIMULATOR
            CHECK(report.backend == MAXPS4_CPU_FEX_ARM64_SIMULATOR);
#else
            CHECK(report.backend == MAXPS4_CPU_FEX_ARM64_JIT);
#endif
            std::printf("PASS M1 FEX backend=%u initial=%u rax=%llu memory=%llu tls=%llu calls=%u entry=0x%llx fs=0x%llx\n",
                report.backend,initial,(unsigned long long)report.rax,(unsigned long long)report.memory,
                (unsigned long long)report.tls_word,report.calls,(unsigned long long)report.entry,(unsigned long long)report.fs_base);
        }
    }
    auto module_a = make(83,false), module_b = make(129,false);
    // Legal plaintext PRX headers; the harness consumes PT_TLS templates only,
    // without claiming PRX relocation/DTV/import support.
    maxps4_ps4_synthetic::put(module_a,0xfe18,16,2);
    maxps4_ps4_synthetic::put(module_b,0xfe18,16,2);
    const maxps4_m1_tls_input modules[] = {{module_a.data(),module_a.size()},{module_b.data(),module_b.size()}};
    file = make(17,true,true);
    for (int run=0;run<2;++run) {
        maxps4_m1_thread_report threads[2]{};
        const int status = maxps4_fex_engine_run_m1_threads(engine,file.data(),file.size(),200,snapshot,
            modules,2,2,threads,2);
        if (status) std::fprintf(stderr,"M1 thread queue rejected: %d\n",status);
        CHECK(status == 0);
        for (size_t i=0;i<2;++i) {
            const auto& t = threads[i];
            CHECK(t.joined == 1 && t.id == i+1 && t.cpu.kernel_return == i+1);
            CHECK(t.cpu.rax == 131 && t.cpu.memory == 131 && t.cpu.tls_word == 22 && t.cpu.heap_word == 22);
            CHECK(t.modules[0] == 84 && t.modules[1] == 131 && !t.zeros[0] && !t.zeros[1]);
            CHECK(!t.cpu.zero_memory && !t.lock_result && !t.unlock_result && t.cpu.calls == 4);
            CHECK(t.cpu.backend == report.backend && !(t.cpu.rsp & 15));
            std::printf("PASS M1 FEX thread=%llu joined=%u backend=%u rax=%llu tls=%llu modules=%llu/%llu mutex=%llu/%llu fs=0x%llx\n",
                (unsigned long long)t.id,t.joined,t.cpu.backend,(unsigned long long)t.cpu.rax,
                (unsigned long long)t.cpu.tls_word,(unsigned long long)t.modules[0],(unsigned long long)t.modules[1],
                (unsigned long long)t.lock_result,(unsigned long long)t.unlock_result,(unsigned long long)t.cpu.fs_base);
        }
        CHECK(threads[0].cpu.rsp != threads[1].cpu.rsp && threads[0].cpu.fs_base != threads[1].cpu.fs_base);
    }
    maxps4_m1_thread_report rejected[2]{};
    CHECK(maxps4_fex_engine_run_m1_threads(engine,file.data(),file.size(),1,snapshot,modules,2,2,rejected,2) == -E2BIG);
    CHECK(!rejected[0].joined && !rejected[1].joined && !rejected[0].cpu.backend);
    CHECK(maxps4_fex_engine_run_m1_threads(engine,file.data(),file.size(),200,snapshot,modules,2,5,rejected,2) == -EINVAL);
    {
        maxps4::GuestAddressSpace blocker(maxps4::GuestAddressSpace::Backend::fex_direct);
        const uint64_t address = maxps4::OrbisM1::thread_base+0x4000000;
        const uint64_t marker = 0xcafebabe;
        CHECK(blocker.reserve(address,0x10000) == 0 && blocker.map(address,0x10000,6,&marker,8) == 0);
        CHECK(maxps4_fex_engine_run_m1_threads(engine,file.data(),file.size(),200,snapshot,modules,2,2,rejected,2) == 0);
        CHECK(rejected[0].joined && rejected[1].joined && rejected[1].cpu.kernel_return == 2);
        uint64_t preserved=0;
        CHECK(blocker.read(address,&preserved,8) == 0 && preserved == marker);
        CHECK(blocker.release(address) == 0);
        // Translated storage leaves occupied host VAs intact; repeat after cleanup.
        CHECK(maxps4_fex_engine_run_m1_threads(engine,file.data(),file.size(),200,snapshot,modules,2,2,rejected,2) == 0);
        CHECK(rejected[0].joined && rejected[1].joined && rejected[1].cpu.kernel_return == 2);
    }
    auto unsupported = make(83,true);
    const maxps4_m1_tls_input relocation_module[] = {{unsupported.data(),unsupported.size()}};
    CHECK(maxps4_fex_engine_run_m1_threads(engine,file.data(),file.size(),200,snapshot,relocation_module,1,2,rejected,2) == -ENOTSUP);
    CHECK(!rejected[0].joined && !rejected[1].joined);
    file = make(); file[0x4000] = 0xeb;
    CHECK(maxps4_fex_engine_run_m1(engine,file.data(),file.size(),100,snapshot,&report) == -ENOTSUP);
    CHECK(!report.rax && !report.backend);
    file = make();
    CHECK(maxps4_fex_engine_run_m1(engine,file.data(),file.size(),1,snapshot,&report) == -E2BIG);
    return 0;
}
int main(int argc, char** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
#if defined(__linux__)
    struct sigaction diagnostic {};
    diagnostic.sa_sigaction=fault_diagnostic;
    diagnostic.sa_flags=SA_SIGINFO|SA_RESETHAND;
    sigemptyset(&diagnostic.sa_mask);
    for (int signal : {SIGSEGV,SIGBUS,SIGILL,SIGABRT}) sigaction(signal,&diagnostic,nullptr);
#endif

    // Keep assertion text visible when the pinned emitter terminates a check.
    LogMan::Throw::InstallHandler([](const char* text) { std::fprintf(stderr,"FEX assertion: %s\n",text); });
    LogMan::Msg::InstallHandler([](LogMan::DebugLevels level,const char* text) {
        if (level<=LogMan::ERROR) std::fprintf(stderr,"FEX error: %s\n",text);
    });

    if (argc == 2 && !std::strcmp(argv[1],"--m2")) {
        CHECK(maxps4_fex_test_m2_low_va(nullptr)==-EINVAL);
        for (unsigned i=0;i<3;++i) {
            maxps4_m2_low_va_report report{};
            CHECK(maxps4_fex_test_m2_low_va(&report)==0);
            CHECK(report.status==0 && report.entry==0x400000 && report.rax==42 && report.memory==42 && report.tls==8);
            CHECK(report.cleanup && report.collision_preserved && report.code_rx && report.stack_rw_nx);
            CHECK(report.boundary_status==0 && report.boundary_cases==16 && report.boundary_preserved==1);
            CHECK(report.backend==MAXPS4_CPU_FEX_ARM64_JIT || report.backend==MAXPS4_CPU_FEX_ARM64_SIMULATOR);
        }
        return 0;
    }
    if (argc == 2 && !std::strcmp(argv[1],"--m1")) return m1Test();
    if (argc == 3 && !std::strcmp(argv[1],"--m1-bundle")) {
        std::filesystem::create_directories(argv[2]);
        for (bool calls : {false,true}) {
            const auto file = orbis_m1_fixture::make(17,calls);
            std::ofstream out(std::filesystem::path(argv[2])/(calls ? "m1-tls-libkernel.elf" : "m1-tls.elf"),std::ios::binary);
            out.write(reinterpret_cast<const char*>(file.data()),file.size()); CHECK(out.good());
        }
        const auto dynamic_file = orbis_m1_fixture::make(17,true,true);
        std::ofstream dynamic_out(std::filesystem::path(argv[2])/"m1-two-threads-three-tls-modules.elf",std::ios::binary);
        dynamic_out.write(reinterpret_cast<const char*>(dynamic_file.data()),dynamic_file.size()); CHECK(dynamic_out.good());
        for (uint32_t initial : {83U,129U}) {
            auto module = orbis_m1_fixture::make(initial,false);
            maxps4_ps4_synthetic::put(module,0xfe18,16,2);
            std::ofstream out(std::filesystem::path(argv[2])/("tls-"+std::to_string(initial)+".prx"),std::ios::binary);
            out.write(reinterpret_cast<const char*>(module.data()),module.size()); CHECK(out.good());
        }
        return 0;
    }
    using namespace ps4_linked_test;
    if (argc==3 && !std::strcmp(argv[1],"--playable-evidence")) return playableTest(argv[2]);
    if (argc==3 && (!std::strcmp(argv[1],"--playable-bundle") || !std::strcmp(argv[1],"--animated-playable-bundle"))) {
        std::filesystem::create_directories(argv[2]);
        const auto file=playable_fixture::app(!std::strcmp(argv[1],"--animated-playable-bundle")), provider=orbis_execution::make(true);
        for (bool prx : {false,true}) {
            const auto& bytes=prx ? provider : file;
            std::ofstream out(std::filesystem::path(argv[2])/(prx ? "provider.prx" : "app.elf"),std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()); CHECK(bool(out));
        }
        return 0;
    }
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if (argc == 3 && !std::strcmp(argv[1],"--orbis-bundle")) {
        const std::filesystem::path directory(argv[2]);
        std::filesystem::create_directories(directory);
        for (bool provider : {false,true}) {
            const auto bytes = orbis_execution::make(provider);
            std::ofstream file(directory/(provider ? "provider.prx" : "app.elf"),std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
            CHECK(bool(file));
        }
        std::ofstream manifest(directory/"maxps4.json");
        manifest << R"({"root":"app.elf","modules":[{"path":"app.elf","loadBias":1048576,"dependencies":["provider.prx"]},{"path":"provider.prx","loadBias":2097152,"dependencies":[]}]})";
        CHECK(bool(manifest));
        return 0;
    }
    // Export the identical tested bytes for the application's import picker.
    if (argc == 2 || argc == 3) {
        for (int i = 1; i < argc; ++i) {
            const auto file = i == 1 ? minimal() : combined();
            FILE* out = std::fopen(argv[i],"wb");
            CHECK(out && std::fwrite(file.data(),1,file.size(),out) == file.size());
            CHECK(std::fclose(out) == 0);
        }
        return 0;
    }
    // Alternate code in the same FEX context: stale translations must never
    // return the previous image's value when mmap reuses its host address.
    for (int restart=0;restart<3;++restart) {
        maxps4_fex_engine* process = nullptr;
        CHECK(maxps4_fex_engine_open(&process)==0 && process);
        struct CloseV3 { maxps4_fex_engine* p; ~CloseV3() { maxps4_fex_engine_close(p); } } closeV3{process};
        for (int repeat=0;repeat<16;++repeat) {
            const uint32_t initial = repeat & 1 ? 7 : 40;
            auto bytes = guest_test::elf(initial, 2, false);
            bytes[7] = 9; guest_test::put(bytes,0xfe00,16,2);
            const maxps4_orbis_module module{"test.elf",bytes.data(),bytes.size(),0,nullptr,0};
            char diagnostic[512]{};
            CHECK(maxps4_fex_engine_prepare_orbis(process,&module,1,"test.elf",64,diagnostic,sizeof(diagnostic))==0);
            uint64_t result=99, logical_entry=99; uint32_t backend=99;
            if (!repeat) {
                CHECK(maxps4_fex_engine_run_orbis(process,&result,&backend,&logical_entry)==-EAGAIN);
                CHECK(result==0 && backend==0 && logical_entry==0);
                CHECK(maxps4_fex_engine_initialize(process)==0);
            }
            CHECK(maxps4_fex_engine_run_orbis(process,&result,&backend,&logical_entry)==0);
            CHECK(result==initial+2 && logical_entry==0x1000);
#ifdef VIXL_SIMULATOR
            CHECK(backend==MAXPS4_CPU_FEX_ARM64_SIMULATOR);
#else
            CHECK(backend==MAXPS4_CPU_FEX_ARM64_JIT);
#endif
            maxps4_fex_cpu_report report{};
            CHECK(maxps4_fex_engine_last_cpu_report(process,&report)==0);
            CHECK(report.completed && report.rax==result && report.entry==logical_entry);
            CHECK(!report.rsp_alignment && report.zero_integer_registers && report.entry_matches);
            CHECK(!report.argc && report.argv_env_null && report.auxv_valid && report.page_size>0);
            uint8_t code[11]{};
            CHECK(maxps4_fex_engine_fetch_ps4(process,0x1000,code,sizeof(code))==0 && code[1]==initial);
            uint32_t permitted=99;
            CHECK(maxps4_fex_engine_query_ps4(process,0x1000,1,2,&permitted)==0 && !permitted);
            CHECK(maxps4_fex_engine_write_ps4(process,0x1000,code,1)==-EFAULT);
            std::printf("PASS V3 ET_SCE_EXEC real FEX restart=%d repeat=%d result=%llu backend=%u entry=0x%llx stack/regs=verified (host evidence only)\n",
                restart,repeat,(unsigned long long)result,backend,(unsigned long long)logical_entry);
            // A failed preparation clears admission; the old program cannot run.
            bytes[120]=0xeb;
            CHECK(maxps4_fex_engine_prepare_orbis(process,&module,1,"test.elf",64,diagnostic,sizeof(diagnostic))!=0);
            CHECK(maxps4_fex_engine_run_orbis(process,&result,&backend,&logical_entry)==-EFAULT);
            CHECK(!result && !backend && !logical_entry);
        }
    }
    CHECK(playableTest()==0);
    CHECK(animatedGuestTest()==0);
    CHECK(maxps4_fex_test_ps4_libkernel(nullptr) == -EINVAL);
    for (size_t repeat = 0; repeat < 3; ++repeat) {
        maxps4_ps4_elf_test_report report{};
        CHECK(maxps4_fex_test_ps4_libkernel(&report) == 0);
        CHECK(!report.open_status && !report.load_status && !report.link_status);
        CHECK(!report.initialize_status && !report.execute_status && !report.read_status && !report.verification_status);
        CHECK(report.entry == entry && report.segments == 2 && report.imports == 1);
        CHECK(report.resolved_service == maxps4_ps4_resolve_libkernel("aI+OeCz8xrQ", "libkernel", "libkernel", 1));
        CHECK(report.calls == 1 && report.resumed == 1 && report.kernel_return == 1);
        CHECK(report.final_memory == 2 && report.rax == 2 && !report.error[0]);
        CHECK(report.guest_segments_after == 0 && report.jit_regions_after == report.jit_regions_before);
#ifdef VIXL_SIMULATOR
        CHECK(report.backend == MAXPS4_CPU_FEX_ARM64_SIMULATOR);
#else
        CHECK(report.backend == MAXPS4_CPU_FEX_ARM64_JIT);
#endif
        std::printf("PASS built-in app ELF diagnostic: entry=0x%llx NID=aI+OeCz8xrQ kernel=%llu resumed=%u memory=%llu backend=%u guest_segments_after=%u jit_leases_after=%u\n",
            (unsigned long long)report.entry, (unsigned long long)report.kernel_return, report.resumed,
            (unsigned long long)report.final_memory, report.backend, report.guest_segments_after, report.jit_regions_after);
    }
    // SCE tables and fabricated NID supplied by an explicit PRX dependency.
    // Observe different provider machine code results; never substitute 42.
    for (uint32_t provider_value : {40U,7U}) {
        auto app = orbis_execution::make(false);
        auto provider = orbis_execution::make(true,provider_value);
        const char* dependency[] = {"provider.prx"};
        const maxps4_orbis_module modules[] = {
            {"app.elf",app.data(),app.size(),orbis_execution::app_bias,dependency,1},
            {"provider.prx",provider.data(),provider.size(),orbis_execution::provider_bias,nullptr,0}};
        maxps4_fex_engine* process = nullptr;
        CHECK(maxps4_fex_engine_open(&process)==0);
        char diagnostic[512]{};
        CHECK(maxps4_fex_engine_prepare_orbis(process,modules,2,"app.elf",6,diagnostic,sizeof(diagnostic))==0);
        uint64_t observed=99, logical_entry=99; uint32_t cpu=99;
        CHECK(maxps4_fex_engine_run_orbis(process,&observed,&cpu,&logical_entry)==-EAGAIN);
        CHECK(!observed && !cpu && !logical_entry);
        CHECK(maxps4_fex_engine_initialize(process)==0);
        for (int repeat=0;repeat<2;++repeat) {
            CHECK(maxps4_fex_engine_run_orbis(process,&observed,&cpu,&logical_entry)==0);
            CHECK(observed==provider_value+2 && logical_entry==orbis_execution::entry);
#ifdef VIXL_SIMULATOR
            CHECK(cpu==MAXPS4_CPU_FEX_ARM64_SIMULATOR);
#else
            CHECK(cpu==MAXPS4_CPU_FEX_ARM64_JIT);
#endif
            uint8_t stored[4]{};
            CHECK(maxps4_fex_engine_read_ps4(process,orbis_execution::data+8,stored,4)==0);
            CHECK(stored[0]==0x34 && stored[1]==0x12 && !stored[2] && !stored[3]);
        }
        std::printf("PASS SCE ELF -> PRX NID/RELA -> FEX CALL/RET -> ADD: RAX=%llu backend=%u entry=0x%llx store=0x1234\n",
            (unsigned long long)observed,cpu,(unsigned long long)logical_entry);
        CHECK(maxps4_fex_engine_prepare_orbis(process,modules,1,"app.elf",6,diagnostic,sizeof(diagnostic))!=0);
        CHECK(maxps4_fex_engine_run_orbis(process,&observed,&cpu,&logical_entry)==-EFAULT);
        CHECK(!observed && !cpu && !logical_entry);
        maxps4_fex_engine_close(process);
    }
    maxps4_fex_engine* engine = nullptr;
    std::puts("BEGIN real FEX context and linked Orbis execution");
    CHECK(maxps4_fex_engine_open(&engine) == 0 && engine);
    struct Close { maxps4_fex_engine* engine; ~Close() { maxps4_fex_engine_close(engine); } } close{engine};
    // Exercise the actual C ABI against a live FEX engine, without V4 mappings.
    // Fail closed: translated/unmapped addresses cannot enter direct FEX execution.
    CHECK(maxps4_fex_engine_v4_preflight(nullptr,0x1000,0x2000,4096)==-EINVAL);
    CHECK(maxps4_fex_engine_v4_preflight(engine,0x1000,0x2000,4096)==-EAGAIN);

    uint64_t entry_out = 99, result = 99;
    uint32_t imports = 99, backend = 99, calls = 99;
    auto file = minimal();
    CHECK(maxps4_fex_engine_link_ps4(engine,file.data(),file.size(),10,&entry_out,&imports) == 0);
    CHECK(entry_out == entry && imports == 1);
    CHECK(maxps4_fex_engine_run_linked_ps4(engine,&result,&backend,&entry_out,&calls) == -EAGAIN);
    CHECK(!result && !backend && !entry_out && !calls);
    std::puts("PASS linking and pre-initialization refusal; initialize real FEX dispatcher");
    CHECK(maxps4_fex_engine_initialize(engine) == 0);
    CHECK(maxps4_fex_engine_v4_preflight(engine,0x1000,0x2000,4096)==-ENOTSUP);
    CHECK(maxps4_fex_engine_v4_preflight(engine,0,0x2000,4096)==-EINVAL);
    CHECK(maxps4_fex_engine_v4_preflight(engine,0x1000,UINT64_MAX,4096)==-EOVERFLOW);

    CHECK(maxps4_fex_engine_probe(engine,&result,&backend) == 0 && result == 42);
    std::puts("PASS existing real FEX JIT 40+2 diagnostic regression (42)");
    std::puts("PASS dispatcher initialization; begin CPU execution");
    for (size_t repeat = 0; repeat < 3; ++repeat) {
        CHECK(maxps4_fex_engine_run_linked_ps4(engine,&result,&backend,&entry_out,&calls) == 0);
        CHECK(result == 2 && calls == 1 && entry_out == entry);
#ifdef VIXL_SIMULATOR
        CHECK(backend == MAXPS4_CPU_FEX_ARM64_SIMULATOR);
#else
        CHECK(backend == MAXPS4_CPU_FEX_ARM64_JIT);
#endif
        uint8_t output[8]{};
        CHECK(maxps4_fex_engine_read_ps4(engine,data+0x100,output,8) == 0 && value(output) == 2);
        CHECK(maxps4_fex_engine_read_ps4(engine,data+0x108,output,8) == 0 && value(output) == 1);
    }
    std::printf("PASS linked Orbis ELF -> NID -> FEX -> libkernel -> resumed ADD/store/HLT (backend=%u)\n",backend);
    // Different post-return arithmetic must produce a different observed value.
    // This detects a bridge that simply substitutes the demo's expected 2.
    file = minimal(); file[entry+14] = 7;
    CHECK(maxps4_fex_engine_link_ps4(engine,file.data(),file.size(),10,&entry_out,&imports) == 0);
    CHECK(maxps4_fex_engine_run_linked_ps4(engine,&result,&backend,&entry_out,&calls) == 0 && result == 8 && calls == 1);
    uint8_t changed[16]{};
    CHECK(maxps4_fex_engine_read_ps4(engine,data+0x100,changed,sizeof(changed)) == 0);
    CHECK(value(changed) == 8 && value(changed+8) == 1);
    std::puts("PASS changed guest ADD produces memory=8, kernel return remains 1; no fixed demo result");
    Program preserved;
    const uint8_t saved[] = {3,5,12,13,14,15}; // RBX, RBP, R12-R15.
    for (auto reg : saved) preserved.reg(reg,0x12340000+reg);
    preserved.call("scePthreadSelf");
    for (size_t i = 0; i < sizeof(saved); ++i) preserved.store(data+0x100+i*8,saved[i]);
    preserved.bytes({0xf4}); file = preserved.elf();
    CHECK(maxps4_fex_engine_link_ps4(engine,file.data(),file.size(),1000,&entry_out,&imports) == 0);
    CHECK(maxps4_fex_engine_run_linked_ps4(engine,&result,&backend,&entry_out,&calls) == 0 && result == 1 && calls == 1);
    uint8_t preserved_bytes[48]{};
    CHECK(maxps4_fex_engine_read_ps4(engine,data+0x100,preserved_bytes,sizeof(preserved_bytes)) == 0);
    for (size_t i = 0; i < sizeof(saved); ++i) CHECK(value(preserved_bytes+i*8) == uint64_t{0x12340000}+saved[i]);
    std::puts("PASS System V callee-saved RBX/RBP/R12-R15 preserved through actual FEX syscall trampoline");
    file = combined();
    CHECK(maxps4_fex_engine_link_ps4(engine,file.data(),file.size(),1000,&entry_out,&imports) == 0 && imports == 5);
    char directory[] = "/tmp/maxps4-linked-XXXXXX";
    CHECK(::mkdtemp(directory));
    char host_path[512]; std::snprintf(host_path,sizeof(host_path),"%s/fixture",directory);
    const int host = ::open(host_path,O_CREAT|O_EXCL|O_WRONLY,0600);
    CHECK(host >= 0 && ::write(host,"PS4!",4) == 4 && ::close(host) == 0);
    struct Remove {
        const char* path; const char* directory;
        ~Remove() { ::unlink(path); ::rmdir(directory); }
    } remove{host_path,directory};
    CHECK(maxps4_fex_engine_mount_app0(engine,directory) == 0);
    for (size_t repeat = 0; repeat < 2; ++repeat) {
        CHECK(maxps4_fex_engine_run_linked_ps4(engine,&result,&backend,&entry_out,&calls) == 0);
        CHECK(result == 0 && calls == 5);
        uint8_t output[48]{};
        CHECK(maxps4_fex_engine_read_ps4(engine,data+0x100,output,sizeof(output)) == 0);
        CHECK(value(output) == 3 && value(output+8) == 4 && value(output+16) == 0 && value(output+24) == 0 && value(output+40) == 0);
        const uint64_t allocation = value(output+32);
        CHECK(allocation >= 0x1000000000ULL);
        uint8_t bytes[16]{};
        CHECK(maxps4_fex_engine_read_ps4(engine,data+0x2000,bytes,4) == 0 && !std::memcmp(bytes,"PS4!",4));
        CHECK(maxps4_fex_engine_read_ps4(engine,data+0x2100,bytes,16) == 0);
        CHECK(value(bytes) > 0 && value(bytes+8) < 1000000000);
        const uint8_t pattern[] = {1,2,3,4};
        CHECK(maxps4_fex_engine_write_ps4(engine,allocation,pattern,sizeof(pattern)) == 0);
        CHECK(maxps4_fex_engine_read_ps4(engine,allocation,bytes,4) == 0 && !std::memcmp(bytes,pattern,4));
    }
    std::puts("PASS real FEX calls open/read/close/clock/mmap, register return, fourth and seventh ABI arguments");
    // A pointer with the numerical value of an application-host address must
    // still be treated as a logical guest pointer and return Orbis EFAULT.
    Program invalid_pointer;
    invalid_pointer.reg(7,reinterpret_cast<uintptr_t>(host_path));
    invalid_pointer.reg(6,0); invalid_pointer.reg(2,0);
    invalid_pointer.call("sceKernelOpen"); invalid_pointer.bytes({0xf4});
    file = invalid_pointer.elf();
    CHECK(maxps4_fex_engine_link_ps4(engine,file.data(),file.size(),100,&entry_out,&imports) == 0);
    CHECK(maxps4_fex_engine_run_linked_ps4(engine,&result,&backend,&entry_out,&calls) == 0);
    CHECK(result == static_cast<uint64_t>(-2147352562LL) && calls == 1);
    CHECK(maxps4_fex_engine_link_ps4(engine,nullptr,0,100,&entry_out,&imports) != 0);
    CHECK(!entry_out && !imports);
    CHECK(maxps4_fex_engine_run_linked_ps4(engine,&result,&backend,&entry_out,&calls) == -EFAULT);
    CHECK(!result && !backend && !entry_out && !calls);
    std::puts("PASS host-pointer isolation, failed restage and cleanup; iPhone execution requires separate device validation");
}



