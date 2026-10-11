// Compile-only ABI checks against the complete matching bundled Darwin headers.
// No allocation, debugger trap, execution or implementation replacement.
#include <FEXCore/Utils/AllocatorHooks.h>
#include "Interface/Context/Context.h"
#include <functional>
#include <type_traits>
#include <utility>

using Thread = FEXCore::Core::InternalThreadState;
using Context = FEXCore::Context::ContextImpl;
using WriteToken = FEXCore::LookupCacheWriteLockToken;
static_assert(std::is_same_v<decltype(&FEXCore::Allocator::GetWritableAddress), void* (*)(void*)>);
static_assert(std::is_same_v<decltype(&FEXCore::Allocator::GetExecutableAddress), void* (*)(void*)>);
static_assert(!std::is_copy_constructible_v<FEXCore::Allocator::ScopedJITWriteProtect>);
static_assert(std::is_default_constructible_v<FEXCore::Allocator::ScopedJITWriteProtect>);
static_assert(std::is_same_v<decltype(std::declval<Context>().OnBufferReusedInPlace),
                           std::function<void(Thread*, const WriteToken&)>>);
static_assert(std::is_same_v<decltype(std::declval<Context>().BeginBufferInvalidationSafepoint),
                           std::function<void(Thread*)>>);
static_assert(std::is_same_v<decltype(std::declval<Context>().EndBufferInvalidationSafepoint),
                           std::function<void(Thread*)>>);
