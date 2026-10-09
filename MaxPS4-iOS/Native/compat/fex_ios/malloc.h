// SPDX-License-Identifier: MIT
// iOS compile-only portability shim for FEXCore's Linux <malloc.h> include.
// Darwin exposes allocator declarations in <stdlib.h> and <malloc/malloc.h>.
// This header must NOT be interpreted as enabling FEXCore's Linux mmap hooks
// or as granting executable memory on iOS.
#pragma once
#if defined(__APPLE__)
#include <stdlib.h>
#include <malloc/malloc.h>
#else
#include_next <malloc.h>
#endif
