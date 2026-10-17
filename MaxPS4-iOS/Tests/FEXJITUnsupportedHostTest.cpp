// Link the public diagnostic/session APIs on hosts without the iOS pool.
// Unsupported publication must never look like a ready JIT session.
#include "maxps4_jit_memory.h"
#include <cassert>
#include <cerrno>
#include <cstdio>

int main() {
    uint64_t mailbox = 99, nonce = 99, page = 99;
    assert(maxps4_jit_begin(nullptr, &nonce, &page) == -EINVAL);
    assert(maxps4_jit_begin(&mailbox, &nonce, &page) == -ENOTSUP);
    assert(mailbox == 0 && nonce == 0 && page == 0);
    assert(maxps4_jit_prepare() == -ENOTSUP);
    assert(maxps4_jit_launch_failed(99) == -ENOTSUP);
    assert(maxps4_jit_confirmation() == 0);
    assert(maxps4_jit_script_failure() == 0);
    assert(maxps4_jit_mailbox_diagnostic() == 0);
    assert(maxps4_jit_state() == 0 && maxps4_jit_error() == 0);
    assert(maxps4_jit_failure_stage() == 0 && maxps4_jit_failure_mach() == 0);
    assert(maxps4_jit_failure_region() == -1 && maxps4_jit_leases() == 0);
    assert(maxps4_jit_usable_size(nullptr, 16384) == 0);
    assert(maxps4_jit_cancel() == 0);
    std::puts("PASS unsupported-host session ABI; no JIT publication or leases");
}

