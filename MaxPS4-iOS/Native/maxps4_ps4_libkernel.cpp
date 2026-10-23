#include "maxps4_ps4_libkernel.h"
#include "maxps4_fex_runtime.h"
#include "maxps4_ps4_kernel_path.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <random>
#include <thread>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace maxps4::ps4 {
Error host_error(int e) {
    switch (e) {
    case 0: return ok;
    case EPERM: return perm;
    case ENOENT: return noent;
    case EINTR: return intr;
    case EIO: return io;
    case EBADF: return badf;
    case ENOMEM: return nomem;
    case EACCES: return access;
    case EFAULT: return fault;
    case EBUSY: return busy;
    case EEXIST: return exist;
    case EXDEV: return xdev;
    case ENOSPC: return nospc;
    case ENOTEMPTY: return notempty;
    case ENOTDIR: return notdir;
    case EISDIR: return isdir;
    case EINVAL: return inval;
    case EMFILE: case ENFILE: return mfile;
    case EROFS: return rofs;
    case EAGAIN: return again;
    case ELOOP: return loop;
    case ENAMETOOLONG: return nametoolong;
    case ENOSYS: return nosys;
    case EOVERFLOW: return overflow;
    case ENOTSUP: return notsup;
    default: return io; // Never leak a host errno with a different Orbis meaning.
    }
}
}
namespace maxps4 {
using namespace ps4;
namespace {
using Binding = PS4KernelExport;
// scePthreadOnce(once, init) as guest code, since init is guest code. The
// control word (FreeBSD): 0 never done, 1 done, 2 in progress. The first
// caller moves it 0 -> 2, calls init and stores 1; others wait for 1.
// Returns 0. Called with RSP = 8 (mod 16), so PUSH RDI aligns the CALL.
constexpr uint8_t once_code[] = {
    0x8b,0x07, 0x83,0xf8,0x01, 0x74,0x21,      // 00 MOV EAX,[RDI]; CMP EAX,1; JE done
    0x31,0xc0, 0xb9,0x02,0x00,0x00,0x00,       // 07 XOR EAX,EAX; MOV ECX,2
    0xf0,0x0f,0xb1,0x0f, 0x75,0x0d,            // 0e LOCK CMPXCHG [RDI],ECX; JNZ wait
    0x57, 0xff,0xd6, 0x5f,                     // 14 PUSH RDI; CALL RSI; POP RDI
    0xc7,0x07,0x01,0x00,0x00,0x00,             // 18 MOV DWORD [RDI],1
    0x31,0xc0, 0xc3,                           // 1e XOR EAX,EAX; RET
    0xf3,0x90, 0x83,0x3f,0x01, 0x75,0xf9,      // 21 wait: PAUSE; CMP DWORD [RDI],1; JNE wait
    0x31,0xc0, 0xc3};                          // 28 done: XOR EAX,EAX; RET
static_assert(sizeof(once_code) == 0x2b, "pthread_once routine layout");
// ABI metadata checked against shadPS4 kernel/file_system.{h,cpp}; original
// implementation below. POSIX variants carry their errno/TLS convention.
constexpr Binding bindings[] = {
    {"sceKernelOpen", "1G3lF1Gg1k8", PS4KernelService::open},
    {"sceKernelRead", "Cg4srZ6TKbU", PS4KernelService::read},
    {"sceKernelClose", "UK2Tl2DWUns", PS4KernelService::close},
    {"sceKernelLseek", "oib76F-12fk", PS4KernelService::lseek},
    {"sceKernelStat", "eV9wAD2riIA", PS4KernelService::stat},
    {"sceKernelFstat", "kBwCPsYX-m4", PS4KernelService::fstat},
    {"sceKernelMmap", "PGhQHd-dzv8", PS4KernelService::mmap},
    {"sceKernelMunmap", "cQke9UuBQOk", PS4KernelService::munmap},
    {"sceKernelMprotect", "vSMAm3cxYTY", PS4KernelService::mprotect},
    {"sceKernelClockGettime", "QBi7HCK03hw", PS4KernelService::clock_gettime},
    {"sceKernelClockGetres", "wRYVA5Zolso", PS4KernelService::clock_getres},
    {"sceKernelGettimeofday", "ejekcaNQNq0", PS4KernelService::gettimeofday},
    {"sceKernelGetProcessTime", "4J2sUJmuHZQ", PS4KernelService::process_time},
    {"scePthreadSelf", "aI+OeCz8xrQ", PS4KernelService::thread_self},
    {"scePthreadMutexInit", "cmo1RIYva9o", PS4KernelService::mutex_init},
    {"scePthreadMutexTrylock", "upoVrzMHFeE", PS4KernelService::mutex_trylock},
    {"scePthreadMutexUnlock", "tn3VlD0hG60", PS4KernelService::mutex_unlock},
    {"scePthreadMutexDestroy", "2Of0f+3mhhE", PS4KernelService::mutex_destroy},
    // NIDs from shadPS4 kernel/threads/mutex.cpp and condvar.cpp (Orbis variants).
    {"scePthreadMutexLock", "9UK1vLZQft4", PS4KernelService::mutex_lock},
    {"scePthreadCondInit", "2Tb92quprl0", PS4KernelService::cond_init},
    {"scePthreadCondDestroy", "g+PZd2hiacg", PS4KernelService::cond_destroy},
    {"scePthreadCondWait", "WKAXJ4XBPQ4", PS4KernelService::cond_wait},
    {"scePthreadCondTimedwait", "BmMjYxmew1w", PS4KernelService::cond_timedwait},
    {"scePthreadCondSignal", "kDh-NfxgMtE", PS4KernelService::cond_signal},
    {"scePthreadCondBroadcast", "JGgj7Uvrl+A", PS4KernelService::cond_broadcast},
    // NIDs from shadPS4 kernel/threads/pthread.cpp, pthread_attr.cpp and
    // mutex.cpp (Orbis variants). Scheduling, affinity, guard size and the
    // mutex protocol are accepted without effect.
    {"scePthreadDetach", "4qGrR6eoP9Y", PS4KernelService::thread_detach},
    {"scePthreadAttrInit", "nsYoNRywwNg", PS4KernelService::attr_init},
    {"scePthreadAttrDestroy", "62KCwEMmzcM", PS4KernelService::attr_destroy},
    {"scePthreadAttrSetdetachstate", "-Wreprtu0Qs", PS4KernelService::attr_set_detach},
    {"scePthreadAttrGetdetachstate", "JaRMy+QcpeU", PS4KernelService::attr_get_detach},
    {"scePthreadAttrSetstacksize", "UTXzJbWhhTE", PS4KernelService::attr_set_stack},
    {"scePthreadAttrGetstacksize", "-fA+7ZlGDQs", PS4KernelService::attr_get_stack},
    {"scePthreadAttrSetschedparam", "DzES9hQF4f4", PS4KernelService::attr_accept},
    {"scePthreadAttrSetinheritsched", "eXbUSpEaTsA", PS4KernelService::attr_accept},
    {"scePthreadAttrSetschedpolicy", "4+h9EzwKF4I", PS4KernelService::attr_accept},
    {"scePthreadAttrSetaffinity", "3qxgM4ezETA", PS4KernelService::attr_accept},
    {"scePthreadAttrSetguardsize", "El+cQ20DynU", PS4KernelService::attr_accept},
    {"scePthreadMutexattrInit", "F8bUHwAG284", PS4KernelService::mutexattr_init},
    {"scePthreadMutexattrInitForInternalLibc", "n2MMpvU8igI", PS4KernelService::mutexattr_init},
    {"scePthreadMutexattrDestroy", "smWEktiyyG0", PS4KernelService::mutexattr_destroy},
    {"scePthreadMutexattrSettype", "iMp8QpE+XO4", PS4KernelService::mutexattr_settype},
    {"scePthreadMutexattrGettype", "gquEhBrS2iw", PS4KernelService::mutexattr_gettype},
    {"scePthreadMutexattrSetprotocol", "1FGvU0i9saQ", PS4KernelService::mutexattr_accept},
    // NIDs from shadPS4 kernel/threads/pthread.cpp (Orbis variants).
    {"scePthreadCreate", "6UgtwV+0zb4", PS4KernelService::thread_create},
    {"scePthreadJoin", "onNY9Byn-W8", PS4KernelService::thread_join},
    {"scePthreadExit", "3kg7rT0NQIs", PS4KernelService::thread_exit},
    // NID from shadPS4 kernel/threads/tcb.cpp (RegisterRtld).
    {"__tls_get_addr", "vNe1w4diLCs", PS4KernelService::tls_get_addr},
    // What a game's libc.prx imports. NIDs from shadPS4 kernel/*.cpp and
    // threads/*.cpp; the last few, absent there, from the NID of their name.
    // POSIX-convention functions set the thread's errno and return -1.
    {"__Error", "9BcDykPmo1I", PS4KernelService::errno_location, "libkernel", true},
    {"exit", "6Z83sYWFlA8", PS4KernelService::process_exit, "libkernel", true, true},
    {"__stack_chk_fail", "Ou3iL1abvng", PS4KernelService::abort_process, "libkernel", true, true},
    {"sceKernelDebugRaiseException", "OMDRKKAZ8I4", PS4KernelService::abort_process, "libkernel", false, true},
    {"sceKernelDebugRaiseExceptionOnReleaseMode", "zE-wXIZjLoM", PS4KernelService::abort_process, "libkernel", false, true},
    {"nanosleep", "NhpspxdjEKU", PS4KernelService::nanosleep, "libkernel", true},
    {"_nanosleep", "yS8U2TGCe1A", PS4KernelService::nanosleep, "libkernel", true},
    {"sceKernelUsleep", "1jfXLRVzisc", PS4KernelService::usleep},
    {"clock_gettime", "lLMT9vJAck0", PS4KernelService::clock_gettime, "libkernel", true},
    {"gettimeofday", "n88vx3C5nW8", PS4KernelService::gettimeofday, "libkernel", true},
    {"getpagesize", "k+AXqu2-eBc", PS4KernelService::page_size, "libkernel", true},
    {"sched_yield", "6XG4B33N09g", PS4KernelService::yield, "libkernel", true},
    {"pthread_yield", "T72hz6ffq08", PS4KernelService::yield, "libkernel", true},
    {"pthread_equal", "3PtV6p3QNX4", PS4KernelService::thread_equal, "libkernel", true},
    {"pthread_getspecific", "eoht7mQOCmo", PS4KernelService::get_specific, "libkernel", true},
    {"scePthreadSetspecific", "+BzXYkqYeLE", PS4KernelService::set_specific},
    {"scePthreadKeyCreate", "geDaqgH9lTg", PS4KernelService::key_create},
    {"scePthreadKeyDelete", "PrdHuuDekhY", PS4KernelService::key_delete},
    {"scePthreadOnce", "14bOACANTBo", PS4KernelService::once, "libkernel", false, false, 0, once_code, sizeof(once_code)},
    {"scePthreadRwlockInit", "6ULAa0fq4jA", PS4KernelService::rwlock_init},
    {"scePthreadRwlockDestroy", "BB+kb08Tl9A", PS4KernelService::rwlock_destroy},
    {"scePthreadRwlockRdlock", "Ox9i0c7L5w0", PS4KernelService::rwlock_rdlock},
    {"scePthreadRwlockWrlock", "mqdNorrB+gI", PS4KernelService::rwlock_wrlock},
    {"scePthreadRwlockTryrdlock", "XD3mDeybCnk", PS4KernelService::rwlock_tryrdlock},
    {"scePthreadRwlockTrywrlock", "bIHoZCTomsI", PS4KernelService::rwlock_trywrlock},
    {"scePthreadRwlockUnlock", "+L98PIbGttk", PS4KernelService::rwlock_unlock},
    {"scePthreadSetprio", "W0Hpm2X0uPE", PS4KernelService::succeed},
    {"scePthreadGetprio", "1tKyG7RlMJo", PS4KernelService::thread_getprio},
    {"scePthreadGetaffinity", "rcrVFJsQWRY", PS4KernelService::thread_getaffinity},
    {"scePthreadSetaffinity", "bt3CTBKmGyI", PS4KernelService::succeed},
    {"scePthreadAttrGetaffinity", "8+s5BzZjxSg", PS4KernelService::attr_getaffinity},
    {"scePthreadAttrGet", "x1X76arYMxU", PS4KernelService::attr_get_np},
    {"sigprocmask", "aPcyptbOiZs", PS4KernelService::succeed, "libkernel", true},
    {"_sigprocmask", "6xVpy0Fdq+I", PS4KernelService::succeed, "libkernel", true},
    {"sigfillset", "VkTAsrZDcJ0", PS4KernelService::sigfillset, "libkernel", true},
    {"signal", "VADc3MNQ3cM", PS4KernelService::signal, "libkernel", true},
    {"sceKernelIsNeoMode", "WslcK1FQcGI", PS4KernelService::succeed},
    {"sceKernelIsAddressSanitizerEnabled", "jh+8XiK4LeE", PS4KernelService::succeed},
    {"sceKernelConvertUtcToLocaltime", "-o5uEDpN+oY", PS4KernelService::utc_to_local},
    {"sceKernelConvertLocaltimeToUtc", "0NTHN1NKONI", PS4KernelService::local_to_utc},
    {"sceKernelGetProcParam", "959qrazPIrg", PS4KernelService::proc_param},
    {"_sceKernelRtldSetApplicationHeapAPI", "p5EcQeEeJAE", PS4KernelService::succeed},
    {"sceKernelGetModuleInfoFromAddr", "f7KBOafysXo", PS4KernelService::module_info_from_addr},
    {"sceKernelGetModuleInfoForUnwind", "RpQJJVKTiFM", PS4KernelService::module_info_from_addr},
    {"sceKernelInternalMemoryGetModuleSegmentInfo", "-YTW+qXc3CQ", PS4KernelService::module_info_from_addr},
    {"__elf_phdr_match_addr", "Fjc4-n1+y2g", PS4KernelService::succeed, "libkernel", true},
    {"sceKernelPrintBacktraceWithModuleInfo", "Wl2o5hOVZdw", PS4KernelService::succeed},
    {"_is_signal_return", "crb5j7mkk1c", PS4KernelService::succeed, "libkernel", true},
    {"sigreturn", "mo0bFmWppIw", PS4KernelService::succeed, "libkernel", true},
    {"__pthread_cxa_finalize", "kbw4UHHSYy0", PS4KernelService::succeed, "libkernel", true},
    {"(libkernel hHlZQUnlxSM)", "hHlZQUnlxSM", PS4KernelService::succeed},
    // Event queue ABI/NIDs: shadPS4 kernel/equeue.cpp. GPU/display filters are
    // deliberately not registered until their actual producers exist.
    {"sceKernelCreateEqueue", "D0OdFMjp46I", PS4KernelService::equeue_create},
    {"sceKernelDeleteEqueue", "jpFjmgAC5AE", PS4KernelService::equeue_delete},
    {"sceKernelWaitEqueue", "fzyMKs9kim0", PS4KernelService::equeue_wait},
    {"sceKernelAddUserEvent", "4R6-OvI2cEA", PS4KernelService::user_event_add},
    {"sceKernelAddUserEventEdge", "WDszmSbWuDk", PS4KernelService::user_event_add_edge},
    {"sceKernelDeleteUserEvent", "LJDwdSNTnDg", PS4KernelService::user_event_delete},
    {"sceKernelTriggerUserEvent", "F6e0kwo4cnk", PS4KernelService::user_event_trigger},
    {"sceKernelGetEventId", "mJ7aghmgvfc", PS4KernelService::event_id},
    {"sceKernelGetEventFilter", "23CPPI1tyBY", PS4KernelService::event_filter},
    {"sceKernelGetEventData", "kwGyyjohI50", PS4KernelService::event_data},
    {"sceKernelGetEventFflags", "Q0qr9AyqJSk", PS4KernelService::event_fflags},
    {"sceKernelGetEventError", "Uu-iDFC9aUc", PS4KernelService::event_error},
    {"sceKernelGetEventUserData", "vz+pg2zdopI", PS4KernelService::event_user_data},
    {"sceGnmAddEqEvent", "b0xyllnVY-I", PS4KernelService::graphics_event_add, "libSceGnmDriver"},
    {"sceGnmDeleteEqEvent", "PVT+fuoS9gU", PS4KernelService::graphics_event_delete, "libSceGnmDriver"},
    {"sceGnmGetEqEventType", "UoYY0DWMC0U", PS4KernelService::event_data, "libSceGnmDriver"},
    {"scePadInit", "hv1luiJrqQM", PS4KernelService::pad_init, "libScePad"},
    {"sceGnmDrawIndexAuto", "GGsn7jMTxw4", PS4KernelService::gnm_draw_auto, "libSceGnmDriver"},
    {"sceGnmSubmitCommandBuffers", "zwY0YV91TTI", PS4KernelService::gnm_submit, "libSceGnmDriver"},
    {"sceGnmAreSubmitsAllowed", "b08AgtPlHPg", PS4KernelService::gnm_allowed, "libSceGnmDriver"},
    {"sceGnmSubmitDone", "yvZ73uQUqrk", PS4KernelService::gnm_done, "libSceGnmDriver"},
    {"sceGnmDrawIndex", "HlTPoZ-oY7Y", PS4KernelService::gnm_draw_index, "libSceGnmDriver"},
    {"sceGnmSetVgtControl", "cFCp0NX8wf0", PS4KernelService::gnm_vgt, "libSceGnmDriver"},
    {"sceGnmSetVsShader", "gAhCn6UiU4Y", PS4KernelService::gnm_vs, "libSceGnmDriver"},
    {"sceGnmSetPsShader350", "5uFKckiJYRM", PS4KernelService::gnm_ps350, "libSceGnmDriver"},
    {"scePadOpen", "xk0AcarP3V4", PS4KernelService::pad_open, "libScePad"},
    {"scePadClose", "6ncge5+l5Qs", PS4KernelService::pad_close, "libScePad"},
    {"scePadRead", "q1cHNfGycLI", PS4KernelService::pad_read, "libScePad"},
    {"scePadReadState", "YndgXqQVV7c", PS4KernelService::pad_read_state, "libScePad"},
    {"scePadGetControllerInformation", "gjP9-KQzoUk", PS4KernelService::pad_information, "libScePad"},
    {"scePadGetHandle", "u1GRHp+oWoY", PS4KernelService::pad_handle, "libScePad"},
    {"sceUserServiceInitialize", "j3YMu1MVNNo", PS4KernelService::user_initialize, "libSceUserService"},
    {"sceUserServiceTerminate", "bwFjS+bX9mA", PS4KernelService::user_terminate, "libSceUserService"},
    {"sceUserServiceGetInitialUser", "CdWp0oHWGr0", PS4KernelService::user_initial, "libSceUserService"},
    {"sceUserServiceGetLoginUserIdList", "fPhymKNvK-A", PS4KernelService::user_login_list, "libSceUserService"},
    {"sceUserServiceGetEvent", "yH17Q6NWtVg", PS4KernelService::user_event, "libSceUserService"},
    {"sceUserServiceGetUserName", "1xxcMiGu2fo", PS4KernelService::user_name, "libSceUserService"},
    {"sceVideoOutOpen", "Up36PTk687E", PS4KernelService::video_open, "libSceVideoOut"},
    {"sceVideoOutClose", "uquVH4-Du78", PS4KernelService::video_close, "libSceVideoOut"},
    {"sceVideoOutSetBufferAttribute", "i6-sR91Wt-4", PS4KernelService::video_attribute, "libSceVideoOut"},
    {"sceVideoOutRegisterBuffers", "w3BY+tAEiQY", PS4KernelService::video_register, "libSceVideoOut"},
    {"sceVideoOutUnregisterBuffers", "N5KDtkIjjJ4", PS4KernelService::video_unregister, "libSceVideoOut"},
    {"sceVideoOutSubmitFlip", "U46NwOiJpys", PS4KernelService::video_submit, "libSceVideoOut"},
    {"sceVideoOutIsFlipPending", "zgXifHT9ErY", PS4KernelService::video_pending, "libSceVideoOut"},
    {"sceVideoOutGetFlipStatus", "SbU3dwp80lQ", PS4KernelService::video_flip_status, "libSceVideoOut"},
    {"sceVideoOutGetVblankStatus", "1FZBKy8HeNU", PS4KernelService::video_vblank_status, "libSceVideoOut"},
    {"sceVideoOutAddFlipEvent", "HXzjK9yI30k", PS4KernelService::video_add_flip, "libSceVideoOut"},
    {"sceVideoOutAddVblankEvent", "Xru92wHJRmg", PS4KernelService::video_add_vblank, "libSceVideoOut"},
    {"sceVideoOutDeleteFlipEvent", "-Ozn0F1AFRg", PS4KernelService::video_delete_flip, "libSceVideoOut"},
    {"sceVideoOutDeleteVblankEvent", "oNOQn3knW6s", PS4KernelService::video_delete_vblank, "libSceVideoOut"},
    {"sceVideoOutGetEventId", "U2JJtSqNKZI", PS4KernelService::video_event_id, "libSceVideoOut"},
    {"sceVideoOutGetEventData", "rWUTcKdkUzQ", PS4KernelService::video_event_data, "libSceVideoOut"},
    {"sceVideoOutGetEventCount", "Mt4QHHkxkOc", PS4KernelService::video_event_count, "libSceVideoOut"},
    {"sceVideoOutGetResolutionStatus", "6kPnj51T62Y", PS4KernelService::video_resolution, "libSceVideoOut"},
    // Flips follow the host display; a requested rate changes nothing.
    {"sceVideoOutSetFlipRate", "CBiu4mCE1DA", PS4KernelService::succeed, "libSceVideoOut"},
    // Memory (shadPS4 kernel/memory.cpp).
    {"sceKernelGetDirectMemorySize", "pO96TwzOm5E", PS4KernelService::direct_size},
    {"sceKernelAllocateDirectMemory", "rTXw65xmLIA", PS4KernelService::direct_allocate},
    {"sceKernelReleaseDirectMemory", "MBuItvba6z8", PS4KernelService::direct_release},
    {"sceKernelMapDirectMemory", "L-Q3LEjIbgA", PS4KernelService::direct_map},
    {"sceKernelMapNamedFlexibleMemory", "mL8NDH86iQI", PS4KernelService::flexible_map},
    {"mmap", "BPE9s9vQQXo", PS4KernelService::posix_mmap, "libkernel", true},
    {"sceLibcHeapGetTraceInfo", "NWtTN10cJzE", PS4KernelService::heap_trace_info, "libSceLibcInternalExt",
     false, false, 0, nullptr, 0, "libSceLibcInternal"},
    {"sceSystemServiceParamGetInt", "fZo48un7LK4", PS4KernelService::system_param_int, "libSceSystemService"},
    {"sceSystemServiceGetStatus", "rPo6tV8D9bM", PS4KernelService::system_status, "libSceSystemService"},
    {"sceSystemServiceReceiveEvent", "656LMQSrg6U", PS4KernelService::system_event, "libSceSystemService"},
    {"sceSystemServiceHideSplashScreen", "Vo5V8KAwCmk", PS4KernelService::succeed, "libSceSystemService"},
    // System modules are part of the HLE: loading one has nothing to do.
    {"sceSysmoduleLoadModule", "g8cM39EUZ6o", PS4KernelService::succeed, "libSceSysmodule"},
    {"msync", "tZY4+SZNFhA", PS4KernelService::succeed, "libkernel", true},
    {"sceKernelMlock", "3k6kx-zOOSQ", PS4KernelService::succeed},
    // Files (shadPS4 kernel/file_system.cpp).
    {"sceKernelWrite", "4wSze92BhLI", PS4KernelService::write},
    {"write", "FxVZqBAA7ks", PS4KernelService::write, "libkernel", true},
    {"read", "DRuBt2pvICk", PS4KernelService::read, "libkernel", true},
    {"sceKernelPread", "+r3rMFwItV4", PS4KernelService::pread},
    {"sceKernelPwrite", "nKWi-N2HBV4", PS4KernelService::pwrite},
    {"sceKernelFsync", "fTx66l5iWIA", PS4KernelService::fsync},
    {"sceKernelFtruncate", "VW3TVZiM4-E", PS4KernelService::ftruncate},
    {"sceKernelTruncate", "WlyEA-sLDf0", PS4KernelService::truncate},
    {"sceKernelMkdir", "1-LFLmRFxxM", PS4KernelService::mkdir},
    {"sceKernelRmdir", "naInUjYt3so", PS4KernelService::rmdir},
    {"sceKernelRename", "52NcYU9+lEo", PS4KernelService::rename},
    {"sceKernelUnlink", "AUXVxWeJU-A", PS4KernelService::unlink},
    {"sceKernelGetdirentries", "taRWhTJFTgE", PS4KernelService::getdirentries},
    // Data objects, filled by PS4Libkernel::initialize_data. "MaxPS4errno" is
    // engine-internal (no game imports it): the per-thread errno cells.
    {"__stack_chk_guard", "f7uOxY9mM1U", PS4KernelService::stack_chk_guard, "libkernel", false, false, 8},
    {"__progname", "djxxOmW6-aw", PS4KernelService::progname, "libkernel", false, false, 64},
    {"(errno table)", "MaxPS4errno", PS4KernelService::errno_table, "libkernel", false, false,
        uint32_t(PS4Threads::thread_limit * 8)}
};
uint64_t monotonic_us() {
    timespec t{};
    if (::clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0;
    return uint64_t(t.tv_sec) * 1000000 + uint64_t(t.tv_nsec) / 1000;
}
class HostFD {
public:
    explicit HostFD(int fd) : fd(fd) {}
    ~HostFD() { if (fd >= 0) ::close(fd); }
    HostFD(const HostFD&) = delete;
    HostFD& operator=(const HostFD&) = delete;
    int fd;
};
}
const PS4KernelExport* libkernel_exports(size_t& count) {
    count = sizeof(bindings) / sizeof(bindings[0]);
    return bindings;
}
PS4KernelService resolve_libkernel(std::string_view symbol, std::string_view library,
                                   std::string_view module, uint32_t version) {
    // A binding's library names its module too (libkernel/libkernel).
    if (version != 1 || library != module) return PS4KernelService::unresolved;
    for (const auto& binding : bindings)
        if (library == binding.library && (symbol == binding.name || symbol == binding.nid)) return binding.service;
    return PS4KernelService::unresolved;
}
PS4Libkernel::PS4Libkernel(PS4GuestMemory& memory, PS4VirtualMemory* dynamic)
    : memory_(memory), dynamic_(dynamic), threads_(memory), videoout_(memory, threads_), user_service_(memory), pad_(memory), gnm_(memory), graphics_(memory), started_us_(monotonic_us()) {}
PS4Libkernel::~PS4Libkernel() {
    reset();
}
void PS4Libkernel::close_files() {
    for (auto& file : files_) { if (file.fd >= 0) ::close(file.fd); file = File{}; }
}
void PS4Libkernel::reset() {
    std::lock_guard<std::mutex> guard(lock_);
    close_files();
    for (auto& mount : mounts_) if (mount.root >= 0) { ::close(mount.root); mount.root = -1; }
    videoout_.reset();
    user_service_.reset();
    pad_.reset();
    graphics_.reset();
    if (dynamic_) dynamic_->reset();
    threads_.reset();
    errno_table_ = errno_table_size_ = process_param_ = heap_trace_ = 0;
    direct_.clear();
    started_us_ = monotonic_us();
}
int64_t PS4Libkernel::mount(const char* guest, const char* directory) {
    std::lock_guard<std::mutex> guard(lock_);
    if (!guest || !directory || !*directory) return kernel_error(inval);
    const auto target = std::find_if(mounts_.begin(), mounts_.end(),
        [guest](const Mount& m) { return !std::strcmp(m.prefix, guest); });
    if (target == mounts_.end()) return kernel_error(inval);
    const int fd = ::open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return kernel_error(host_error(errno));
    close_files();
    if (target->root >= 0) ::close(target->root);
    target->root = fd;
    return 0;
}
Error PS4Libkernel::resolve(uint64_t address, size_t& mount, char* relative) const {
    char path[PS4KernelPath::max_path];
    if (!memory_.read_cstring(address, path, sizeof(path))) return fault;
    // Absolute virtual paths only; no "." or ".." components, no "//", no
    // control characters or backslashes, no trailing "/" (except a mount root).
    if (path[0] != '/') return inval;
    size_t length = std::strlen(path);
    for (size_t i = 1; i < length; ++i) {
        const unsigned char byte = static_cast<unsigned char>(path[i]);
        if (byte < 0x20 || byte == 0x7f || byte == '\\') return inval;
        if (path[i] == '/' && path[i - 1] == '/') return inval;
        if (path[i - 1] == '/' && path[i] == '.' &&
            (path[i + 1] == '/' || !path[i + 1] || (path[i + 1] == '.' && (path[i + 2] == '/' || !path[i + 2]))))
            return inval;
    }
    for (size_t m = 0; m < mounts_.size(); ++m) {
        const size_t n = std::strlen(mounts_[m].prefix);
        if (std::strncmp(path, mounts_[m].prefix, n) || (path[n] && path[n] != '/')) continue;
        const char* rest = path + n + (path[n] == '/' ? 1 : 0);
        if (*rest && rest[std::strlen(rest) - 1] == '/') return inval;
        mount = m;
        std::strcpy(relative, rest);
        return mounts_[m].root < 0 ? noent : ok;
    }
    return length > 1 && path[length - 1] == '/' ? inval : noent;
}
int PS4Libkernel::parent(size_t mount, const char* relative, char* leaf, Error& error) const {
    // Walk from a held directory descriptor; reject symlinks at EVERY component.
    // Do not canonicalize then reopen a host path (a race can escape the mount).
    HostFD directory(::fcntl(mounts_[mount].root, F_DUPFD_CLOEXEC, 0));
    if (directory.fd < 0) { error = host_error(errno); return -1; }
    char path[PS4KernelPath::max_path];
    std::strcpy(path, *relative ? relative : ".");
    char* component = path;
    while (char* separator = std::strchr(component, '/')) {
        *separator = '\0';
        const int next = ::openat(directory.fd, component, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next < 0) { error = host_error(errno); return -1; }
        ::close(directory.fd); directory.fd = next;
        component = separator + 1;
    }
    std::strcpy(leaf, component);
    error = ok;
    const int result = directory.fd;
    directory.fd = -1;
    return result;
}
bool PS4Libkernel::writable(uint64_t address, size_t size) const {
    return address && memory_.permitted(address, size, 2) && !memory_.permitted(address, size, 1);
}
PS4Libkernel::File* PS4Libkernel::file(int32_t fd) {
    return fd >= 3 && size_t(fd) < files_.size() && files_[fd].fd >= 0 ? &files_[fd] : nullptr;
}
int64_t PS4Libkernel::open(const PS4KernelCall& c) {
    // FreeBSD flags: access mode (0/1/2), APPEND 8, SYNC 0x80, CREAT 0x200,
    // TRUNC 0x400, EXCL 0x800, DIRECTORY 0x20000.
    const uint32_t flags = uint32_t(c.rsi), access = flags & 3U;
    if (access == 3U) return kernel_error(inval);
    const bool writing = access != 0 || (flags & (0x8U | 0x200U | 0x400U));
    size_t mount = 0;
    char relative[PS4KernelPath::max_path], leaf[PS4KernelPath::max_path];
    Error e = resolve(c.rdi, mount, relative);
    if (e) return kernel_error(e);
    if (writing && !mounts_[mount].writable) return kernel_error(rofs);
    if (flags & ~(3U | 0x8U | 0x80U | 0x200U | 0x400U | 0x800U | 0x20000U)) return kernel_error(notsup);
    size_t slot = 3; // Standard streams are not host stdin/stdout/stderr aliases.
    while (slot < files_.size() && files_[slot].fd >= 0) ++slot;
    if (slot == files_.size()) return kernel_error(mfile);
    HostFD directory(parent(mount, relative, leaf, e));
    if (directory.fd < 0) return kernel_error(e);
    // NONBLOCK avoids blocking on FIFOs/devices before fstat rejects them.
    const int host_flags = (access == 0 ? O_RDONLY : access == 1 ? O_WRONLY : O_RDWR) |
        ((flags & 0x8U) ? O_APPEND : 0) | ((flags & 0x80U) ? O_SYNC : 0) |
        ((flags & 0x200U) ? O_CREAT : 0) | ((flags & 0x400U) ? O_TRUNC : 0) |
        ((flags & 0x800U) ? O_EXCL : 0) | ((flags & 0x20000U) ? O_DIRECTORY : 0) |
        O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
    const int fd = ::openat(directory.fd, leaf, host_flags, 0666);
    if (fd < 0) return kernel_error(host_error(errno));
    struct ::stat info{};
    if (::fstat(fd, &info) != 0) { const Error error = host_error(errno); ::close(fd); return kernel_error(error); }
    if (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode)) { ::close(fd); return kernel_error(notsup); }
    files_[slot] = File{fd, access != 1, access != 0, S_ISDIR(info.st_mode), false, !mounts_[mount].writable, {}, 0};
    return int64_t(slot);
}
int64_t PS4Libkernel::close(int32_t fd) {
    File* f = file(fd);
    if (!f) return kernel_error(badf);
    const int host = f->fd;
    *f = File{};
    // Never retry close after EINTR: the host descriptor may already be released.
    return ::close(host) == 0 ? 0 : kernel_error(host_error(errno));
}
int64_t PS4Libkernel::read(const PS4KernelCall& c) {
    File* f = file(signed32(c.rdi));
    if (!f || !f->readable) return kernel_error(badf);
    if (c.rdx > transfer_limit) return kernel_error(inval);
    const size_t count = size_t(c.rdx);
    if (count && !writable(c.rsi, count)) return kernel_error(fault);
    if (f->directory) return kernel_error(isdir);
    if (!count) return 0;
    std::vector<uint8_t> buffer(count); // Allocate before advancing file offset.
    ssize_t got;
    do { got = ::read(f->fd, buffer.data(), count); } while (got < 0 && errno == EINTR);
    if (got < 0) return kernel_error(host_error(errno));
    if (got && !memory_.write(c.rsi, buffer.data(), size_t(got))) return kernel_error(fault);
    return got;
}
int64_t PS4Libkernel::lseek(const PS4KernelCall& c) {
    File* f = file(signed32(c.rdi));
    if (!f) return kernel_error(badf);
    int whence;
    switch (uint32_t(c.rdx)) {
    case 0: whence = SEEK_SET; break;
    case 1: whence = SEEK_CUR; break;
    case 2: whence = SEEK_END; break;
    default: return kernel_error(inval);
    }
    if (f->directory) {
        // A directory's offset is its position in the dirent records.
        const int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? int64_t(f->position) : int64_t(f->entries.size());
        const int64_t offset = signed64(c.rsi);
        if (offset > INT64_MAX - base) return kernel_error(overflow);
        if (offset < -base) return kernel_error(inval);
        const int64_t target = base + offset;
        if (target % 512) return kernel_error(inval);
        f->position = uint64_t(target);
        return target;
    }
    static_assert(sizeof(off_t) == 8, "PS4 requires 64-bit file offsets");
    const off_t result = ::lseek(f->fd, signed64(c.rsi), whence);
    return result < 0 ? kernel_error(host_error(errno)) : int64_t(result);
}
int64_t PS4Libkernel::stat(const PS4KernelCall& c, bool by_descriptor) {
    if (!writable(c.rsi, stat_size)) return kernel_error(fault);
    struct ::stat info{};
    bool read_only = true;
    if (by_descriptor) {
        File* f = file(signed32(c.rdi));
        if (!f) return kernel_error(badf);
        if (::fstat(f->fd, &info) != 0) return kernel_error(host_error(errno));
        read_only = f->read_only_mount;
    } else {
        size_t mount = 0;
        char relative[PS4KernelPath::max_path], leaf[PS4KernelPath::max_path];
        Error e = resolve(c.rdi, mount, relative);
        if (e) return kernel_error(e);
        HostFD directory(parent(mount, relative, leaf, e));
        if (directory.fd < 0) return kernel_error(e);
        if (::fstatat(directory.fd, leaf, &info, AT_SYMLINK_NOFOLLOW) != 0) return kernel_error(host_error(errno));
        if (S_ISLNK(info.st_mode)) return kernel_error(loop);
        if (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode)) return kernel_error(notsup);
        read_only = !mounts_[mount].writable;
    }
    uint8_t wire[stat_size] = {};
    // OrbisKernelStat is 120 bytes; encode fields explicitly, never copy host stat.
    put_le(wire, 0, info.st_dev, 4); put_le(wire, 4, info.st_ino, 4);
    const uint16_t mode = (S_ISDIR(info.st_mode) ? 0040000 : 0100000) |
        (uint16_t(info.st_mode) & 0777U & (read_only ? ~0222U : 0777U));
    put_le(wire, 8, mode, 2); put_le(wire, 10, info.st_nlink, 2);
    put_le(wire, 12, info.st_uid, 4); put_le(wire, 16, info.st_gid, 4);
#if defined(__APPLE__)
    const auto at = info.st_atimespec, mt = info.st_mtimespec, ct = info.st_ctimespec;
    put_le(wire, 104, info.st_birthtimespec.tv_sec, 8);
    put_le(wire, 112, info.st_birthtimespec.tv_nsec, 8);
#else
    const auto at = info.st_atim, mt = info.st_mtim, ct = info.st_ctim;
#endif
    put_le(wire, 24, at.tv_sec, 8); put_le(wire, 32, at.tv_nsec, 8);
    put_le(wire, 40, mt.tv_sec, 8); put_le(wire, 48, mt.tv_nsec, 8);
    put_le(wire, 56, ct.tv_sec, 8); put_le(wire, 64, ct.tv_nsec, 8);
    put_le(wire, 72, info.st_size, 8); put_le(wire, 80, info.st_blocks, 8);
    put_le(wire, 88, info.st_blksize, 4);
    return memory_.write(c.rsi, wire, sizeof(wire)) ? 0 : kernel_error(fault);
}
int64_t PS4Libkernel::file_service(PS4KernelService service, const PS4KernelCall& c) {
    using S = PS4KernelService;
    switch (service) {
    // write(fd, buffer, count) and pwrite(fd, buffer, count, offset). The
    // game's stdout and stderr (1, 2) go to the engine's standard error.
    case S::write: case S::pwrite: {
        if (c.rdx > transfer_limit) return kernel_error(inval);
        if (service == S::pwrite && signed64(c.rcx) < 0) return kernel_error(inval);
        const size_t count = size_t(c.rdx);
        std::vector<uint8_t> buffer(count);
        if (count && !memory_.read(c.rsi, buffer.data(), count)) return kernel_error(fault);
        const int32_t fd = signed32(c.rdi);
        if ((fd == 1 || fd == 2) && service == S::write) {
            if (count) std::fwrite(buffer.data(), 1, count, stderr);
            return int64_t(count);
        }
        File* f = file(fd);
        if (!f || !f->writable) return kernel_error(badf);
        if (f->directory) return kernel_error(isdir);
        ssize_t put;
        do {
            put = service == S::write ? ::write(f->fd, buffer.data(), count) :
                ::pwrite(f->fd, buffer.data(), count, off_t(signed64(c.rcx)));
        } while (put < 0 && errno == EINTR);
        return put < 0 ? kernel_error(host_error(errno)) : int64_t(put);
    }
    // pread(fd, buffer, count, offset).
    case S::pread: {
        File* f = file(signed32(c.rdi));
        if (!f || !f->readable) return kernel_error(badf);
        if (c.rdx > transfer_limit || signed64(c.rcx) < 0) return kernel_error(inval);
        const size_t count = size_t(c.rdx);
        if (count && !writable(c.rsi, count)) return kernel_error(fault);
        if (f->directory) return kernel_error(isdir);
        std::vector<uint8_t> buffer(count);
        ssize_t got;
        do { got = ::pread(f->fd, buffer.data(), count, off_t(signed64(c.rcx))); } while (got < 0 && errno == EINTR);
        if (got < 0) return kernel_error(host_error(errno));
        if (got && !memory_.write(c.rsi, buffer.data(), size_t(got))) return kernel_error(fault);
        return got;
    }
    case S::fsync: {
        File* f = file(signed32(c.rdi));
        if (!f) return kernel_error(badf);
        return ::fsync(f->fd) == 0 ? 0 : kernel_error(host_error(errno));
    }
    case S::ftruncate: {
        File* f = file(signed32(c.rdi));
        if (!f || !f->writable) return kernel_error(badf);
        if (signed64(c.rsi) < 0) return kernel_error(inval);
        return ::ftruncate(f->fd, off_t(c.rsi)) == 0 ? 0 : kernel_error(host_error(errno));
    }
    // Path operations, on writable mounts only.
    case S::truncate: case S::mkdir: case S::rmdir: case S::unlink: case S::rename: {
        size_t mount = 0, other = 0;
        char relative[PS4KernelPath::max_path], leaf[PS4KernelPath::max_path];
        char relative2[PS4KernelPath::max_path], leaf2[PS4KernelPath::max_path];
        Error e = resolve(c.rdi, mount, relative);
        if (e) return kernel_error(e);
        if (!mounts_[mount].writable) return kernel_error(rofs);
        if (!*relative && service != S::truncate) return kernel_error(service == S::mkdir ? exist : busy);
        HostFD directory(parent(mount, relative, leaf, e));
        if (directory.fd < 0) return kernel_error(e);
        int result = -1;
        switch (service) {
        case S::truncate: {
            if (signed64(c.rsi) < 0) return kernel_error(inval);
            HostFD target(::openat(directory.fd, leaf, O_WRONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
            if (target.fd < 0) return kernel_error(host_error(errno));
            struct ::stat info{};
            if (::fstat(target.fd, &info) != 0) return kernel_error(host_error(errno));
            if (!S_ISREG(info.st_mode)) return kernel_error(notsup);
            result = ::ftruncate(target.fd, off_t(c.rsi));
            break;
        }
        case S::mkdir: result = ::mkdirat(directory.fd, leaf, 0777); break;
        case S::rmdir: result = ::unlinkat(directory.fd, leaf, AT_REMOVEDIR); break;
        case S::unlink: result = ::unlinkat(directory.fd, leaf, 0); break;
        default: { // rename(from, to): both in the same writable mount.
            e = resolve(c.rsi, other, relative2);
            if (e) return kernel_error(e);
            if (other != mount) return kernel_error(xdev);
            if (!*relative2) return kernel_error(busy);
            HostFD directory2(parent(other, relative2, leaf2, e));
            if (directory2.fd < 0) return kernel_error(e);
            result = ::renameat(directory.fd, leaf, directory2.fd, leaf2);
        }
        }
        return result == 0 ? 0 : kernel_error(host_error(errno));
    }
    // getdirentries(fd, buffer, bytes, s64* base): whole 512-byte blocks of
    // FreeBSD dirent records {u32 fileno; u16 reclen; u8 type; u8 namlen;
    // name}, each record 4-byte aligned and the last of a block stretched to
    // its end (as shadPS4's NormalDirectory).
    case S::getdirentries: {
        File* f = file(signed32(c.rdi));
        if (!f) return kernel_error(badf);
        if (!f->directory) return kernel_error(inval);
        if (c.rdx < 512 || c.rdx > transfer_limit) return kernel_error(inval);
        if (c.rcx && !writable(c.rcx, 8)) return kernel_error(fault);
        if (!f->listed) {
            const int copy = ::fcntl(f->fd, F_DUPFD_CLOEXEC, 0);
            if (copy < 0) return kernel_error(host_error(errno));
            struct Listing {
                DIR* value;
                ~Listing() { if (value) ::closedir(value); }
            } listing{::fdopendir(copy)};
            if (!listing.value) { const Error error = host_error(errno); ::close(copy); return kernel_error(error); }
            ::rewinddir(listing.value);
            std::vector<uint8_t> entries;
            size_t offset = 0, ceiling = 0, last = 0;
            uint32_t number = 1;
            for (;;) {
                errno = 0;
                const dirent* entry = ::readdir(listing.value);
                if (!entry) {
                    if (errno) return kernel_error(host_error(errno));
                    break;
                }
                const size_t length = std::strlen(entry->d_name);
                if (length > 255) continue;
                const size_t record = (8 + length + 1 + 3) & ~size_t{3};
                if (offset + record > ceiling) {
                    if (ceiling) put_le(entries.data(), last + 4, get_le(entries.data() + last + 4, 2) + (ceiling - offset), 2);
                    offset = ceiling;
                    ceiling += 512;
                    entries.resize(ceiling, 0);
                }
                last = offset;
                put_le(entries.data(), offset, number++, 4);
                put_le(entries.data(), offset + 4, record, 2);
                entries[offset + 6] = entry->d_type == DT_DIR ? 4 : entry->d_type == DT_REG ? 8 :
                    entry->d_type == DT_LNK ? 10 : 0; // FreeBSD DT_DIR / DT_REG / DT_LNK / DT_UNKNOWN.
                entries[offset + 7] = uint8_t(length);
                std::memcpy(entries.data() + offset + 8, entry->d_name, length);
                offset += record;
            }
            if (ceiling) put_le(entries.data(), last + 4, get_le(entries.data() + last + 4, 2) + (ceiling - offset), 2);
            f->entries = std::move(entries);
            f->listed = true;
        }
        const uint64_t start = std::min<uint64_t>(f->position, f->entries.size());
        const uint64_t count = std::min<uint64_t>(c.rdx & ~uint64_t{511}, f->entries.size() - start);
        if (count && !writable(c.rsi, size_t(count))) return kernel_error(fault);
        if (c.rcx) {
            uint8_t wire[8];
            put_le(wire, 0, start, 8);
            if (!memory_.write(c.rcx, wire, 8)) return kernel_error(fault);
        }
        if (count && !memory_.write(c.rsi, f->entries.data() + start, size_t(count))) return kernel_error(fault);
        f->position = start + count;
        return int64_t(count);
    }
    default: return kernel_error(nosys);
    }
}
int64_t PS4Libkernel::mmap(const PS4KernelCall& c) {
    if (!dynamic_) return kernel_error(nosys);
    // The seventh System V argument is at entry RSP+8, after return address.
    uint8_t pointer[8];
    if (!c.rsp || c.rsp > UINT64_MAX - 8 || !memory_.read(c.rsp + 8, pointer, 8))
        return kernel_error(fault);
    const uint64_t output = get_le(pointer, 8);
    if (!writable(output, 8)) return kernel_error(fault);
    uint64_t mapped = 0;
    const Error e = dynamic_->map(c.rdi, c.rsi, uint32_t(c.rdx), uint32_t(c.rcx),
                                 signed32(c.r8), signed64(c.r9), mapped);
    if (e) return kernel_error(e);
    put_le(pointer, 0, mapped, 8);
    if (!memory_.write(output, pointer, 8)) {
        dynamic_->unmap(mapped, c.rsi);
        return kernel_error(fault);
    }
    return 0;
}
int64_t PS4Libkernel::clock(const PS4KernelCall& c, bool resolution) {
    clockid_t id;
    switch (uint32_t(c.rdi)) {
    case 0: id = CLOCK_REALTIME; break;
    case 4: id = CLOCK_MONOTONIC; break;
    default: return kernel_error(inval);
    }
    if (!writable(c.rsi, 16)) return kernel_error(fault);
    timespec time{};
    const int result = resolution ? ::clock_getres(id, &time) : ::clock_gettime(id, &time);
    if (result != 0) return kernel_error(host_error(errno));
    uint8_t wire[16]{};
    put_le(wire, 0, time.tv_sec, 8); put_le(wire, 8, time.tv_nsec, 8);
    return memory_.write(c.rsi, wire, sizeof(wire)) ? 0 : kernel_error(fault);
}
int64_t PS4Libkernel::gettimeofday(uint64_t output) {
    if (!writable(output, 16)) return kernel_error(fault);
    timeval time{};
    if (::gettimeofday(&time, nullptr) != 0) return kernel_error(host_error(errno));
    uint8_t wire[16]{};
    put_le(wire, 0, time.tv_sec, 8); put_le(wire, 8, time.tv_usec, 8);
    return memory_.write(output, wire, sizeof(wire)) ? 0 : kernel_error(fault);
}
int64_t PS4Libkernel::posix_result(int64_t result, uint64_t thread) {
    // Orbis kernel errors are kernel_error(1..) = -(0x7ffe0000 - e).
    constexpr int64_t first = -int64_t(0x7ffe0000) + 1, last = -int64_t(0x7ffe0000) + 0xffff;
    if (result < first || result > last) return result;
    const size_t index = threads_.index(thread);
    if (errno_table_ && index < errno_table_size_ / 8) {
        uint8_t wire[4];
        put_le(wire, 0, uint64_t(result + 0x7ffe0000), 4);
        (void)memory_.write(errno_table_ + index * 8, wire, sizeof(wire));
    }
    return -1;
}
int64_t PS4Libkernel::invoke(PS4KernelService service, const PS4KernelCall& c, uint64_t thread, bool posix) {
    bool handled = false;
    int64_t result = extended(service, c, thread, handled);
    if (!handled) result = invoke_service(service, c, thread);
    return posix ? posix_result(result, thread) : result;
}
int64_t PS4Libkernel::extended(PS4KernelService service, const PS4KernelCall& c, uint64_t thread, bool& handled) {
    handled = true;
    auto write = [&](uint64_t address, uint64_t value, size_t width) {
        uint8_t wire[8];
        put_le(wire, 0, value, width);
        return address && memory_.write(address, wire, width);
    };
    using S = PS4KernelService;
    switch (service) {
    case S::user_initialize: return user_service_.initialize(c.rdi);
    case S::pad_init: return pad_.initialize();
    case S::gnm_draw_auto: return gnm_.draw_auto(c.rdi,uint32_t(c.rsi),uint32_t(c.rdx),uint32_t(c.rcx));
    case S::gnm_submit: return graphics_.submit(uint32_t(c.rdi),c.rsi,c.rdx,c.rcx,c.r8);
    case S::gnm_allowed: return graphics_.allowed();
    case S::gnm_done: return graphics_.done();
    case S::gnm_draw_index: return gnm_.draw_index(c.rdi,uint32_t(c.rsi),uint32_t(c.rdx),c.rcx,uint32_t(c.r8));
    case S::gnm_vgt: return gnm_.vgt(c.rdi,uint32_t(c.rsi),uint32_t(c.rdx),uint32_t(c.rcx),uint32_t(c.r8));
    case S::gnm_vs: return gnm_.vs(c.rdi,uint32_t(c.rsi),c.rdx,uint32_t(c.rcx));
    case S::gnm_ps350: return gnm_.ps350(c.rdi,uint32_t(c.rsi),c.rdx);
    case S::pad_open: return pad_.open(signed32(c.rdi), signed32(c.rsi), signed32(c.rdx), c.rcx);
    case S::pad_close: return pad_.close(signed32(c.rdi));
    case S::pad_handle: return pad_.get_handle(signed32(c.rdi), signed32(c.rsi), signed32(c.rdx));
    case S::pad_read: return pad_.read(signed32(c.rdi), c.rsi, signed32(c.rdx), false);
    case S::pad_read_state: return pad_.read(signed32(c.rdi), c.rsi, 1, true);
    case S::pad_information: return pad_.information(signed32(c.rdi), c.rsi);
    case S::user_terminate: return user_service_.terminate();
    case S::user_initial: return user_service_.initial_user(c.rdi);
    case S::user_login_list: return user_service_.login_users(c.rdi);
    case S::user_event: return user_service_.event(c.rdi);
    case S::user_name: return user_service_.user_name(signed32(c.rdi), c.rsi, c.rdx);
    case S::video_open: return videoout_.open(signed32(c.rsi), signed32(c.rdx), c.rcx);
    case S::video_close: return videoout_.close(c.rdi);
    case S::video_attribute: {
        uint8_t pitch[8];
        if (!c.rsp || c.rsp > UINT64_MAX - 8 || !memory_.read(c.rsp + 8, pitch, 8)) return PS4VideoOut::error(2);
        return videoout_.set_attribute(c.rdi, uint32_t(c.rsi), uint32_t(c.rdx), uint32_t(c.rcx), uint32_t(c.r8), uint32_t(c.r9), uint32_t(get_le(pitch, 8)));
    }
    case S::video_register: return videoout_.register_buffers(c.rdi, signed32(c.rsi), c.rdx, signed32(c.rcx), c.r8);
    case S::video_unregister: return videoout_.unregister_buffers(c.rdi, signed32(c.rsi));
    case S::video_submit: return videoout_.submit(c.rdi, signed32(c.rsi), signed32(c.rdx), c.rcx);
    case S::video_pending: return videoout_.pending(c.rdi);
    case S::video_resolution: return videoout_.resolution(c.rdi, c.rsi);
    case S::video_flip_status: return videoout_.flip_status(c.rdi, c.rsi);
    case S::video_vblank_status: return videoout_.vblank_status(c.rdi, c.rsi);
    case S::video_add_flip: case S::video_add_vblank:
        return videoout_.add_event(c.rdi, c.rsi, service == S::video_add_flip ? 6 : 7, c.rdx);
    case S::video_delete_flip: case S::video_delete_vblank:
        return videoout_.add_event(c.rdi, c.rsi, service == S::video_delete_flip ? 6 : 7, 0, true);
    case S::video_event_id: case S::video_event_data: case S::video_event_count:
        return videoout_.event_field(c.rdi, c.rsi, service == S::video_event_id ? 0 : service == S::video_event_data ? 1 : 2);
    case S::graphics_event_add: return kernel_error(threads_.graphics_event_add(c.rdi, c.rsi, c.rdx));
    case S::graphics_event_delete: return kernel_error(threads_.graphics_event_delete(c.rdi, c.rsi));
    case S::equeue_create: return kernel_error(threads_.equeue_create(c.rdi, c.rsi));
    case S::equeue_delete: return kernel_error(threads_.equeue_delete(c.rdi));
    case S::equeue_wait:
        return kernel_error(threads_.equeue_wait(c.rdi, c.rsi, signed32(c.rdx), c.rcx, c.r8));
    case S::user_event_add: case S::user_event_add_edge:
        return kernel_error(threads_.user_event_add(c.rdi, signed32(c.rsi), service == S::user_event_add_edge));
    case S::user_event_delete: return kernel_error(threads_.user_event_delete(c.rdi, signed32(c.rsi)));
    case S::user_event_trigger: return kernel_error(threads_.user_event_trigger(c.rdi, signed32(c.rsi), c.rdx));
    case S::event_id: case S::event_filter: case S::event_data: case S::event_fflags:
    case S::event_error: case S::event_user_data: {
        uint8_t event[PS4Threads::event_size];
        if (!memory_.read(c.rdi, event, sizeof(event))) return kernel_error(fault);
        if (service == S::event_id) return signed64(get_le(event, 8));
        if (service == S::event_data) return signed64(get_le(event + 16, 8));
        if (service == S::event_user_data) return signed64(get_le(event + 24, 8));
        if (service == S::event_filter) {
            const uint32_t filter = uint32_t(get_le(event + 8, 2));
            return filter <= INT16_MAX ? int64_t(filter) : int64_t(filter) - 0x10000;
        }
        const uint32_t flags = uint32_t(get_le(event + 12, 4));
        return service == S::event_error && flags ? signed32(0x80020000U | flags) : int64_t(flags);
    }
    case S::errno_location: {
        const size_t index = threads_.index(thread);
        return errno_table_ && index < errno_table_size_ / 8 ? int64_t(errno_table_ + index * 8) : kernel_error(fault);
    }
    case S::nanosleep: {
        // nanosleep(const timespec* request, timespec* remaining).
        uint8_t wire[16];
        if (!memory_.read(c.rdi, wire, sizeof(wire))) return kernel_error(fault);
        const int64_t seconds = signed64(get_le(wire, 8)), nanoseconds = signed64(get_le(wire + 8, 8));
        if (seconds < 0 || nanoseconds < 0 || nanoseconds >= 1000000000) return kernel_error(inval);
        const Error slept = threads_.sleep(uint64_t(seconds) * 1000000 + uint64_t(nanoseconds) / 1000);
        if (c.rsi && (!write(c.rsi, 0, 8) || !write(c.rsi + 8, 0, 8))) return kernel_error(fault);
        return kernel_error(slept);
    }
    case S::usleep: return kernel_error(threads_.sleep(c.rdi));
    case S::page_size: return 16384; // PS4 pages are 16 KiB.
    case S::yield: std::this_thread::yield(); return 0;
    case S::thread_equal: return c.rdi == c.rsi;
    case S::key_create: return kernel_error(threads_.key_create(c.rdi, c.rsi));
    case S::key_delete: return kernel_error(threads_.key_delete(c.rdi));
    case S::get_specific: return int64_t(threads_.get_specific(c.rdi, thread));
    case S::set_specific: return kernel_error(threads_.set_specific(c.rdi, c.rsi, thread));
    case S::rwlock_init: return kernel_error(threads_.rwlock_init(c.rdi));
    case S::rwlock_destroy: return kernel_error(threads_.rwlock_destroy(c.rdi));
    case S::rwlock_rdlock: return kernel_error(threads_.rwlock_lock(c.rdi, thread, false, true));
    case S::rwlock_wrlock: return kernel_error(threads_.rwlock_lock(c.rdi, thread, true, true));
    case S::rwlock_tryrdlock: return kernel_error(threads_.rwlock_lock(c.rdi, thread, false, false));
    case S::rwlock_trywrlock: return kernel_error(threads_.rwlock_lock(c.rdi, thread, true, false));
    case S::rwlock_unlock: return kernel_error(threads_.rwlock_unlock(c.rdi, thread));
    // scePthreadGetprio(thread, int* prio): the default priority, 700.
    case S::thread_getprio: return write(c.rsi, 700, 4) ? 0 : kernel_error(fault);
    // Affinity masks: the 7 cores a game may use.
    case S::thread_getaffinity: return write(c.rsi, 0x7f, 8) ? 0 : kernel_error(fault);
    case S::attr_getaffinity:
        if (const Error e = threads_.attr_accept(c.rdi)) return kernel_error(e);
        return write(c.rsi, 0x7f, 8) ? 0 : kernel_error(fault);
    // pthread_attr_get_np(thread, attr*): the attribute object keeps its defaults.
    case S::attr_get_np: return kernel_error(threads_.attr_accept(c.rsi));
    case S::sigfillset: // sigset_t: 128 bits.
        return write(c.rdi, UINT64_MAX, 8) && write(c.rdi + 8, UINT64_MAX, 8) ? 0 : kernel_error(fault);
    case S::signal: return 0; // SIG_DFL: signals are never delivered.
    // sceKernelConvertUtcToLocaltime(time, time_t* local, Timesec* st, u64* dst):
    // the local time zone is UTC.
    case S::utc_to_local:
        if (c.rsi && !write(c.rsi, c.rdi, 8)) return kernel_error(fault);
        if (c.rdx && (!write(c.rdx, c.rdi, 8) || !write(c.rdx + 8, 0, 8))) return kernel_error(fault);
        if (c.rcx && !write(c.rcx, 0, 8)) return kernel_error(fault);
        return 0;
    // sceKernelConvertLocaltimeToUtc(time, unused, time_t* utc, Timezone* tz, s32* dst).
    case S::local_to_utc:
        if (!c.r8 && !c.rcx) return kernel_error(inval);
        if (c.rcx && !write(c.rcx, 0, 8)) return kernel_error(fault);
        if (c.rdx && !write(c.rdx, c.rdi, 8)) return kernel_error(fault);
        if (c.r8 && !write(c.r8, 0, 4)) return kernel_error(fault);
        return 0;
    case S::write: case S::pread: case S::pwrite: case S::fsync: case S::ftruncate: case S::truncate:
    case S::mkdir: case S::rmdir: case S::rename: case S::unlink: case S::getdirentries: {
        std::lock_guard<std::mutex> guard(lock_);
        if (!threads_.known(thread)) return kernel_error(inval);
        try { return file_service(service, c); } catch (const std::bad_alloc&) { return kernel_error(nomem); }
    }
    case S::direct_size: case S::direct_allocate: case S::direct_release:
    case S::direct_map: case S::flexible_map: case S::posix_mmap:
        return memory_service(service, c);
    case S::proc_param: return int64_t(process_param_);
    // sceLibcHeapGetTraceInfo(info{u64 size; u32 flag, get_segment_info;
    // u64* mspace_atomic_id_mask; u64* mstate_table}), as shadPS4: no segment
    // info, a zero mask and a zeroed 64-entry table that libc's heap updates.
    case S::heap_trace_info: {
        uint8_t size[8];
        if (!c.rdi || !memory_.read(c.rdi, size, 8)) return kernel_error(fault);
        if (get_le(size, 8) < 0x20) return kernel_error(inval);
        std::lock_guard<std::mutex> guard(lock_);
        if (!heap_trace_ && (!dynamic_ ||
                dynamic_->place(0, PS4VirtualMemory::page_size, 0, 3, false, heap_trace_))) {
            heap_trace_ = 0;
            return kernel_error(nomem);
        }
        return write(c.rdi + 12, 0, 4) && write(c.rdi + 16, heap_trace_, 8) &&
               write(c.rdi + 24, heap_trace_ + 8, 8) ? 0 : kernel_error(fault);
    }
    // sceSystemServiceParamGetInt(id, s32* value): language 1 (English US),
    // cross to enter (1000), UTC with default formats and no parental level.
    case S::system_param_int: {
        constexpr int64_t parameter_error = int32_t(0x80A10003);
        int32_t value = 0;
        switch (c.rdi) {
        case 1: value = 1; break;
        case 2: case 3: case 4: case 5: case 7: value = 0; break;
        case 1000: value = 1; break;
        default: return parameter_error;
        }
        if (!c.rsi) return parameter_error;
        return write(c.rsi, uint32_t(value), 4) ? 0 : kernel_error(fault);
    }
    // sceSystemServiceGetStatus(status): no events, in the foreground, CPU
    // mode 7 normal (offset 6); 128 reserved bytes stay zero.
    case S::system_status: {
        if (!c.rdi) return int32_t(0x80A10003);
        const uint8_t status[136] = {0, 0, 0, 0, 0, 0, 1};
        return memory_.write(c.rdi, status, sizeof(status)) ? 0 : kernel_error(fault);
    }
    case S::system_event: return int32_t(0x80A10004); // No event.
    case S::module_info_from_addr: return kernel_error(srch);
    case S::succeed: return 0;
    default: handled = false; return 0;
    }
}
int64_t PS4Libkernel::memory_service(PS4KernelService service, const PS4KernelCall& c) {
    using S = PS4KernelService;
    // The PS4's direct memory: 5056 MiB of physical address space.
    constexpr uint64_t direct_total = uint64_t(5056) * 1024 * 1024, page = PS4VirtualMemory::page_size;
    if (service == S::direct_size) return int64_t(direct_total);
    std::lock_guard<std::mutex> guard(lock_);
    if (!dynamic_) return kernel_error(nosys);
    auto aligned = [&](uint64_t value) { return value % page == 0; };
    // CPU read/write bits; GPU-only memory (0x10/0x20) is CPU read/write here,
    // since the GPU is emulated on the CPU. Execution is refused.
    auto cpu = [](uint64_t protection, uint32_t& out) {
        if (protection & ~uint64_t{0x33}) return false;
        out = uint32_t(protection & 3);
        if (!out && (protection & 0x30)) out = 3;
        return true;
    };
    auto read_pointer = [&](uint64_t address, uint64_t& value) {
        uint8_t wire[8];
        if (!address || !memory_.read(address, wire, 8)) return false;
        value = get_le(wire, 8);
        return true;
    };
    auto write_pointer = [&](uint64_t address, uint64_t value) {
        uint8_t wire[8]; put_le(wire, 0, value, 8);
        return memory_.write(address, wire, 8);
    };
    switch (service) {
    // sceKernelAllocateDirectMemory(start, end, length, alignment, type, s64* physical):
    // the first fit in [start, end), aligned.
    case S::direct_allocate: {
        const int64_t start = signed64(c.rdi), end = signed64(c.rsi);
        const uint64_t length = c.rdx, alignment = c.rcx ? c.rcx : page;
        if (start < 0 || end < 0 || !length || !aligned(length) || !aligned(alignment) ||
            (alignment & (alignment - 1)) || uint32_t(c.r8) > 10 || !c.r9) return kernel_error(inval);
        if (length > direct_total) return kernel_error(again);
        const uint64_t limit = std::min<uint64_t>(uint64_t(end), direct_total);
        uint64_t candidate = (uint64_t(start) + alignment - 1) & ~(alignment - 1);
        for (const auto& [at, bytes] : direct_) {
            if (at + bytes <= candidate) continue;
            if (candidate + length <= at) break;
            candidate = (at + bytes + alignment - 1) & ~(alignment - 1);
        }
        if (candidate > limit || length > limit - candidate) return kernel_error(again);
        if (!write_pointer(c.r9, candidate)) return kernel_error(fault);
        direct_[candidate] = length;
        return 0;
    }
    // sceKernelReleaseDirectMemory(start, length): whole allocations in the range.
    case S::direct_release: {
        if (!aligned(c.rdi) || !c.rsi || !aligned(c.rsi)) return kernel_error(inval);
        bool released = false;
        for (auto it = direct_.lower_bound(c.rdi); it != direct_.end() && it->first < c.rdi + c.rsi;) {
            it = direct_.erase(it);
            released = true;
        }
        return released ? 0 : kernel_error(noent);
    }
    // sceKernelMapDirectMemory(void** address, length, protection, flags, physical, alignment):
    // the physical range must be allocated; *address is a hint, or exact with MAP_FIXED.
    case S::direct_map: {
        uint64_t hint = 0;
        uint32_t protection = 0;
        if (!read_pointer(c.rdi, hint)) return kernel_error(fault);
        if (!c.rsi || !aligned(c.rsi) || !aligned(c.r8) || !cpu(c.rdx, protection)) return kernel_error(inval);
        auto owner = direct_.upper_bound(c.r8);
        if (owner == direct_.begin() || std::prev(owner)->first + std::prev(owner)->second < c.r8 + c.rsi)
            return kernel_error(inval);
        uint64_t mapped = 0;
        const Error e = dynamic_->place(hint, c.rsi, std::max<uint64_t>(c.r9, page), protection, (c.rcx & 0x10) != 0, mapped);
        if (e) return kernel_error(e);
        return write_pointer(c.rdi, mapped) ? 0 : (dynamic_->unmap(mapped, c.rsi), kernel_error(fault));
    }
    // sceKernelMapNamedFlexibleMemory(void** address, length, protection, flags, name).
    case S::flexible_map: {
        uint64_t hint = 0;
        uint32_t protection = 0;
        char name[32];
        if (!read_pointer(c.rdi, hint)) return kernel_error(fault);
        if (!c.r8 || !memory_.read_cstring(c.r8, name, sizeof(name))) return kernel_error(fault);
        if (!c.rsi || !aligned(c.rsi) || !cpu(c.rdx, protection)) return kernel_error(inval);
        uint64_t mapped = 0;
        const Error e = dynamic_->place(hint, c.rsi, page, protection, (c.rcx & 0x10) != 0 && hint, mapped);
        if (e) return kernel_error(e);
        return write_pointer(c.rdi, mapped) ? 0 : (dynamic_->unmap(mapped, c.rsi), kernel_error(fault));
    }
    // mmap(address, length, protection, flags, fd, offset): anonymous mappings only.
    case S::posix_mmap: {
        uint32_t protection = 0;
        if (!c.rsi || !cpu(c.rdx, protection)) return kernel_error(inval);
        if (!(c.rcx & 0x1000) || signed32(c.r8) != -1) return kernel_error(nosys);
        uint64_t mapped = 0;
        const Error e = dynamic_->place(c.rdi, c.rsi, page, protection, (c.rcx & 0x10) != 0, mapped);
        return e ? kernel_error(e) : int64_t(mapped);
    }
    default: return kernel_error(nosys);
    }
}
bool PS4Libkernel::initialize_data(PS4KernelService service, uint64_t address, uint64_t size) {
    switch (service) {
    case PS4KernelService::stack_chk_guard: {
        std::random_device random;
        const uint64_t guard = (uint64_t(random()) << 32) | random();
        uint8_t wire[8];
        put_le(wire, 0, guard, 8);
        return size >= 8 && memory_.write(address, wire, 8);
    }
    case PS4KernelService::progname: {
        // char* __progname, pointing at the name stored right after it.
        static constexpr char name[] = "eboot.bin";
        uint8_t wire[8];
        put_le(wire, 0, address + 16, 8);
        return size >= 16 + sizeof(name) && memory_.write(address, wire, 8) &&
            memory_.write(address + 16, name, sizeof(name));
    }
    case PS4KernelService::errno_table:
        errno_table_ = address;
        errno_table_size_ = size;
        return true;
    default: return false;
    }
}
int64_t PS4Libkernel::invoke_service(PS4KernelService service, const PS4KernelCall& c, uint64_t thread) {
    // Thread services use only PS4Threads (its own lock): a thread blocked in
    // a lock or a wait must not hold up the thread that would release it.
    switch (service) {
    case PS4KernelService::thread_self: return threads_.known(thread) ? int64_t(thread) : kernel_error(inval);
    case PS4KernelService::mutex_init:
        return threads_.known(thread) ? kernel_error(threads_.init(c.rdi, c.rsi, c.rdx)) : kernel_error(inval);
    case PS4KernelService::mutex_trylock: return kernel_error(threads_.trylock(c.rdi, thread));
    case PS4KernelService::mutex_lock: return kernel_error(threads_.lock(c.rdi, thread));
    case PS4KernelService::mutex_unlock: return kernel_error(threads_.unlock(c.rdi, thread));
    case PS4KernelService::mutex_destroy:
        return threads_.known(thread) ? kernel_error(threads_.destroy(c.rdi)) : kernel_error(inval);
    case PS4KernelService::cond_init:
        return threads_.known(thread) ? kernel_error(threads_.cond_init(c.rdi, c.rsi, c.rdx)) : kernel_error(inval);
    case PS4KernelService::cond_destroy:
        return threads_.known(thread) ? kernel_error(threads_.cond_destroy(c.rdi)) : kernel_error(inval);
    case PS4KernelService::cond_wait: return kernel_error(threads_.cond_wait(c.rdi, c.rsi, thread, false, 0));
    // scePthreadCondTimedwait(cond, mutex, usec): a relative timeout.
    case PS4KernelService::cond_timedwait: return kernel_error(threads_.cond_wait(c.rdi, c.rsi, thread, true, c.rdx));
    case PS4KernelService::cond_signal:
    case PS4KernelService::cond_broadcast:
        return threads_.known(thread) ?
            kernel_error(threads_.cond_signal(c.rdi, service == PS4KernelService::cond_broadcast)) : kernel_error(inval);
    case PS4KernelService::attr_init: return kernel_error(threads_.attr_init(c.rdi));
    case PS4KernelService::attr_destroy: return kernel_error(threads_.attr_destroy(c.rdi));
    case PS4KernelService::attr_set_detach: return kernel_error(threads_.attr_set_detach(c.rdi, c.rsi));
    case PS4KernelService::attr_get_detach: return kernel_error(threads_.attr_get_detach(c.rdi, c.rsi));
    case PS4KernelService::attr_set_stack: return kernel_error(threads_.attr_set_stack(c.rdi, c.rsi));
    case PS4KernelService::attr_get_stack: return kernel_error(threads_.attr_get_stack(c.rdi, c.rsi));
    case PS4KernelService::attr_accept: return kernel_error(threads_.attr_accept(c.rdi));
    case PS4KernelService::mutexattr_init: return kernel_error(threads_.mutexattr_init(c.rdi));
    case PS4KernelService::mutexattr_destroy: return kernel_error(threads_.mutexattr_destroy(c.rdi));
    case PS4KernelService::mutexattr_settype: return kernel_error(threads_.mutexattr_settype(c.rdi, c.rsi));
    case PS4KernelService::mutexattr_gettype: return kernel_error(threads_.mutexattr_gettype(c.rdi, c.rsi));
    case PS4KernelService::mutexattr_accept: return kernel_error(threads_.mutexattr_accept(c.rdi));
    default: break;
    }
    std::lock_guard<std::mutex> guard(lock_);
    try {
        if (!threads_.known(thread)) return kernel_error(inval);
        switch (service) {
        case PS4KernelService::open: return open(c);
        case PS4KernelService::read: return read(c);
        case PS4KernelService::close: return close(signed32(c.rdi));
        case PS4KernelService::lseek: return lseek(c);
        case PS4KernelService::stat: return stat(c, false);
        case PS4KernelService::fstat: return stat(c, true);
        case PS4KernelService::mmap: return mmap(c);
        case PS4KernelService::munmap: return kernel_error(dynamic_ ? dynamic_->unmap(c.rdi, c.rsi) : nosys);
        case PS4KernelService::mprotect: return kernel_error(dynamic_ ? dynamic_->protect(c.rdi, c.rsi, uint32_t(c.rdx)) : nosys);
        case PS4KernelService::clock_gettime: return clock(c, false);
        case PS4KernelService::clock_getres: return clock(c, true);
        case PS4KernelService::gettimeofday: return gettimeofday(c.rdi);
        case PS4KernelService::process_time: {
            const uint64_t now = monotonic_us();
            return now >= started_us_ ? int64_t(now - started_us_) : 0;
        }
        default: return kernel_error(nosys);
        }
    } catch (const std::bad_alloc&) { return kernel_error(nomem); }
}
} // namespace maxps4
extern "C" uint32_t maxps4_ps4_resolve_libkernel(const char* symbol, const char* library,
                                               const char* module, uint32_t version) {
    if (!symbol || !library || !module) return 0;
    return uint32_t(maxps4::resolve_libkernel(symbol, library, module, version));
}
