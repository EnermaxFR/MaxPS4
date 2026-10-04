// SPDX-License-Identifier: GPL-2.0-or-later
#include "MaxPS4CoreBridge.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

#include "emulator.h"

namespace {
std::mutex g_mutex;
std::unique_ptr<Core::Emulator> g_emulator;
std::filesystem::path g_eboot;
std::atomic_bool g_running{false};
}

extern "C" int maxps4_core_initialize(const MaxPS4CoreOptions* options) {
    std::scoped_lock lock{g_mutex};
    if (g_emulator) {
        return 0;
    }

    try {
        g_emulator = std::make_unique<Core::Emulator>();
        (void)options;
        return 0;
    } catch (...) {
        g_emulator.reset();
        return -1;
    }
}

extern "C" int maxps4_core_prepare_game(const char* eboot_path) {
    if (eboot_path == nullptr || *eboot_path == '\0') {
        return -1;
    }

    std::scoped_lock lock{g_mutex};
    if (!g_emulator || g_running.load()) {
        return -2;
    }

    g_eboot = std::filesystem::path{eboot_path};
    return 0;
}

extern "C" int maxps4_core_run(void) {
    std::filesystem::path eboot;
    Core::Emulator* emulator = nullptr;
    {
        std::scoped_lock lock{g_mutex};
        if (!g_emulator || g_eboot.empty() || g_running.exchange(true)) {
            return -1;
        }
        emulator = g_emulator.get();
        eboot = g_eboot;
    }

    try {
        emulator->Run(eboot);
        g_running.store(false);
        return 0;
    } catch (...) {
        g_running.store(false);
        return -2;
    }
}

extern "C" void maxps4_core_shutdown(void) {
    std::scoped_lock lock{g_mutex};
    if (g_emulator) {
        g_emulator->Shutdown();
    }
    g_running.store(false);
}

extern "C" int maxps4_core_is_running(void) {
    return g_running.load() ? 1 : 0;
}
