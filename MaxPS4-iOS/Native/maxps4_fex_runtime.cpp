#include "maxps4_fex_runtime.h"
#include "Common/HostFeatures.h"
#include <FEXCore/Config/Config.h>
#include <FEXCore/Core/Context.h>
#include <FEXCore/Core/SignalDelegator.h>
#include <TargetConditionals.h>
#include <cerrno>
#include <mutex>
#include <new>

extern "C" int maxps4_native_guest_x86_run(const uint8_t*, size_t, uint32_t,
                                           uint64_t*) noexcept;
struct maxps4_fex_engine {
    // This real FEX configuration holder installs no process signal handlers.
    // It outlives the context. Guest execution is deliberately not exposed yet.
    FEXCore::SignalDelegator signals;
    fextl::unique_ptr<FEXCore::Context::Context> context;
    int32_t stage = MAXPS4_FEX_CONTEXT_CREATED;
};
namespace {
// FEX configuration is process-global. This adapter owns it until the last
// engine closes. Future independent FEX consumers must share this ownership.
std::mutex runtime_lock;
size_t engines = 0;
void release_config_if_unused() {
    if (engines == 0) FEXCore::Config::Shutdown();
}
}
extern "C" int32_t maxps4_fex_engine_open(maxps4_fex_engine **out) {
    if (!out) return -EINVAL;
    *out = nullptr;
    std::lock_guard lock(runtime_lock);
    auto *engine = new (std::nothrow) maxps4_fex_engine;
    if (!engine) return -ENOMEM;
    if (engines == 0) {
        FEXCore::Config::Initialize();
        FEXCore::Config::Load();
        FEXCore::Config::Set(FEXCore::Config::CONFIG_IS64BIT_MODE, "1");
        FEXCore::Config::Set(FEXCore::Config::CONFIG_DISABLETELEMETRY, "1");
        FEXCore::Config::Set(FEXCore::Config::CONFIG_ENABLECODECACHINGWIP, "0");
    }
    // Real Darwin sysctl-based feature discovery; never fabricated features.
    const auto features = FEX::FetchHostFeatures();
    engine->context = FEXCore::Context::Context::CreateNewContext(features);
    if (!engine->context) {
        delete engine;
        release_config_if_unused();
        return -ENOMEM;
    }
    // InitCore is explicit; opening a context never enables guest execution.
    ++engines;
    *out = engine;
    return 0;
}
extern "C" void maxps4_fex_engine_close(maxps4_fex_engine *engine) {
    if (!engine) return;
    std::lock_guard lock(runtime_lock);
    delete engine; // Context must die before global configuration shutdown.
    --engines;
    release_config_if_unused();
}
extern "C" int32_t maxps4_fex_engine_initialize(maxps4_fex_engine *engine) {
    if (!engine) return -EINVAL;
    std::lock_guard lock(runtime_lock);
#if TARGET_OS_IPHONE
    // The pinned allocator calls BreakpointJIT, not an OS-provided JIT allocator.
    // Do not load it or attempt allocation without a supported device contract.
    return -ENOTSUP;
#else
    if (engine->stage == MAXPS4_FEX_DISPATCHER_INITIALIZED) return 0;
    engine->context->SetSignalDelegator(&engine->signals);
    errno = 0;
    if (!engine->context->InitCore()) return -(errno ? errno : ENOMEM);
    engine->stage = MAXPS4_FEX_DISPATCHER_INITIALIZED;
    return 0;
#endif
}
extern "C" int32_t maxps4_fex_engine_stage(const maxps4_fex_engine *engine) {
    std::lock_guard lock(runtime_lock);
    return engine ? engine->stage : 0;
}
extern "C" int32_t maxps4_fex_engine_cpuid(maxps4_fex_engine *engine,
                                          uint32_t leaf, uint32_t subleaf,
                                          uint32_t out[4]) {
    if (!engine || !out) return -EINVAL;
    std::lock_guard lock(runtime_lock);
    const auto values = engine->context->RunCPUIDFunction(leaf, subleaf);
    out[0] = values.eax;
    out[1] = values.ebx;
    out[2] = values.ecx;
    out[3] = values.edx;
    return 0;
}
extern "C" int32_t maxps4_fex_engine_run(maxps4_fex_engine *engine,
                                        const uint8_t *code, size_t length,
                                        uint32_t budget, uint64_t *result,
                                        uint32_t *backend) {
    if (result) *result = 0;
    if (backend) *backend = 0;
    if (!engine || !code || !length || length > 65536 || !budget ||
        budget > 1000000 || !result || !backend) return -EINVAL;
    if (maxps4_native_guest_x86_run(code, length, budget, result) != 1) {
        *result = 0;
        return -ENOTSUP;
    }
    *backend = MAXPS4_CPU_RESTRICTED_INTERPRETER;
    return 0;
}
