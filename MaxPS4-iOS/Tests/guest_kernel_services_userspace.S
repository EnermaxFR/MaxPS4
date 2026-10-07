/*
 * SPDX-License-Identifier: MIT
 *
 * MaxPS4Kernel service integration smoke.
 * No Sony code, firmware, keys, SDK, or proprietary content.
 *
 * Covers:
 *   virtual /app0 + /savedata open/read/write/close
 *   managed mmap/mprotect/munmap bookkeeping
 *   thr_self logical identity
 *   logical thread create/complete/join
 *   logical recursive mutex create/lock/unlock/destroy
 *   realtime/monotonic clocks, nanosleep, scheduler yield
 *   FreeBSD legacy umtx + _umtx_op uncontended mutex/wake paths
 *   real FreeBSD thr_new guest execution on a second FEX context
 *   blocking UMTX_OP_WAIT_UINT_PRIVATE / WAKE_PRIVATE between FEX threads
 */

.section .text
.globl _start
.type _start,@function
_start:
    sub $512, %rsp

    /* open/read/close a seeded read-only /app0 file */
    mov $5, %rax
    lea app0_path(%rip), %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    js failed
    mov %rax, %r12

    mov $3, %rax
    mov %r12, %rdi
    lea 0(%rsp), %rsi
    mov $32, %rdx
    syscall
    cmp $13, %rax
    jne failed
    cmpb $'M', 0(%rsp)
    jne failed
    cmpb $'4', 5(%rsp)
    jne failed
    cmpb $10, 12(%rsp)
    jne failed

    mov $6, %rax
    mov %r12, %rdi
    syscall
    test %rax, %rax
    jne failed

    /* write then reopen/read persistent /savedata state */
    mov $5, %rax
    lea save_path(%rip), %rdi
    mov $2, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    js failed
    mov %rax, %r12

    mov $4, %rax
    mov %r12, %rdi
    lea save_payload(%rip), %rsi
    mov $9, %rdx
    syscall
    cmp $9, %rax
    jne failed

    mov $6, %rax
    mov %r12, %rdi
    syscall
    test %rax, %rax
    jne failed

    mov $5, %rax
    lea save_path(%rip), %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    js failed
    mov %rax, %r12

    mov $3, %rax
    mov %r12, %rdi
    lea 64(%rsp), %rsi
    mov $9, %rdx
    syscall
    cmp $9, %rax
    jne failed
    movabs $0x6b6f2d6574617473, %rax
    cmp %rax, 64(%rsp)
    jne failed
    cmpb $10, 72(%rsp)
    jne failed

    mov $6, %rax
    mov %r12, %rdi
    syscall
    test %rax, %rax
    jne failed

    /* managed anonymous mapping: 16 KiB RW -> R -> unmap */
    mov $477, %rax
    xor %rdi, %rdi
    mov $16384, %rsi
    mov $3, %rdx
    xor %r10, %r10
    mov $-1, %r8
    xor %r9, %r9
    syscall
    test %rax, %rax
    jz failed
    mov %rax, %r13

    movabs $0x4d4d4c454e52454b, %rax /* "KERNELMM" little-endian */
    mov %rax, 0(%r13)

    mov $74, %rax
    mov %r13, %rdi
    mov $16384, %rsi
    mov $1, %rdx
    syscall
    test %rax, %rax
    jne failed

    movabs $0x4d4d4c454e52454b, %rax
    cmp %rax, 0(%r13)
    jne failed

    mov $73, %rax
    mov %r13, %rdi
    mov $16384, %rsi
    syscall
    test %rax, %rax
    jne failed

    /* current logical thread id */
    movq $0, 128(%rsp)
    mov $432, %rax
    lea 128(%rsp), %rdi
    syscall
    test %rax, %rax
    jne failed
    cmpq $1, 128(%rsp)
    jne failed

    /* wall clock: gettimeofday */
    movq $0, 144(%rsp)
    movq $0, 152(%rsp)
    mov $116, %rax
    lea 144(%rsp), %rdi
    xor %rsi, %rsi
    syscall
    test %rax, %rax
    jne failed
    cmpq $0, 144(%rsp)
    jle failed
    cmpq $0, 152(%rsp)
    jl failed
    cmpq $1000000, 152(%rsp)
    jae failed

    /* monotonic clock + resolution */
    movq $0, 160(%rsp)
    movq $0, 168(%rsp)
    mov $232, %rax
    mov $4, %rdi
    lea 160(%rsp), %rsi
    syscall
    test %rax, %rax
    jne failed
    cmpq $0, 160(%rsp)
    jl failed
    cmpq $0, 168(%rsp)
    jl failed
    cmpq $1000000000, 168(%rsp)
    jae failed

    movq $-1, 176(%rsp)
    movq $-1, 184(%rsp)
    mov $234, %rax
    mov $4, %rdi
    lea 176(%rsp), %rsi
    syscall
    test %rax, %rax
    jne failed
    cmpq $0, 176(%rsp)
    jne failed
    cmpq $1, 184(%rsp)
    jne failed

    /* one millisecond kernel sleep + scheduler yield */
    movq $0, 192(%rsp)
    movq $1000000, 200(%rsp)
    movq $-1, 208(%rsp)
    movq $-1, 216(%rsp)
    mov $240, %rax
    lea 192(%rsp), %rdi
    lea 208(%rsp), %rsi
    syscall
    test %rax, %rax
    jne failed
    cmpq $0, 208(%rsp)
    jne failed
    cmpq $0, 216(%rsp)
    jne failed

    mov $331, %rax
    syscall
    test %rax, %rax
    jne failed

    /* legacy _umtx_lock / _umtx_unlock */
    movq $0, 224(%rsp)
    mov $434, %rax
    lea 224(%rsp), %rdi
    syscall
    test %rax, %rax
    jne failed
    cmpq $1, 224(%rsp)
    jne failed

    mov $435, %rax
    lea 224(%rsp), %rdi
    syscall
    test %rax, %rax
    jne failed
    cmpq $0, 224(%rsp)
    jne failed

    /* _umtx_op umutex lock/unlock, then private wake */
    movl $0, 232(%rsp)
    movl $0, 236(%rsp)
    mov $454, %rax
    lea 232(%rsp), %rdi
    mov $5, %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    xor %r8, %r8
    syscall
    test %rax, %rax
    jne failed
    cmpl $1, 232(%rsp)
    jne failed

    mov $454, %rax
    lea 232(%rsp), %rdi
    mov $6, %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    xor %r8, %r8
    syscall
    test %rax, %rax
    jne failed
    cmpl $0, 232(%rsp)
    jne failed

    movl $0, 240(%rsp)
    mov $454, %rax
    lea 240(%rsp), %rdi
    mov $16, %rsi
    mov $1, %rdx
    xor %r10, %r10
    xor %r8, %r8
    syscall
    test %rax, %rax
    jne failed

    /*
     * Real FreeBSD thr_new: reserve a guest stack in the managed arena,
     * create a second FEX context, and let it publish its own thr_self TID.
     */
    mov $477, %rax
    xor %rdi, %rdi
    mov $16384, %rsi
    mov $3, %rdx
    xor %r10, %r10
    mov $-1, %r8
    xor %r9, %r9
    syscall
    test %rax, %rax
    jz failed
    mov %rax, %r13

    /* shared result: marker, child_tid, parent_tid, worker_self, futex */
    movq $0, 368(%rsp)
    movq $0, 376(%rsp)
    movq $0, 384(%rsp)
    movq $0, 392(%rsp)
    movl $0, 400(%rsp)
    movq $5, 408(%rsp)       /* timeout seconds */
    movq $0, 416(%rsp)       /* timeout nanoseconds */

    /* struct thr_param at rsp+256 (104 bytes on x86-64 FreeBSD). */
    lea worker_start(%rip), %rax
    mov %rax, 256(%rsp)
    lea 368(%rsp), %rax
    mov %rax, 264(%rsp)
    mov %r13, 272(%rsp)
    movq $16384, 280(%rsp)
    movq $0, 288(%rsp)
    movq $0, 296(%rsp)
    lea 376(%rsp), %rax
    mov %rax, 304(%rsp)
    lea 384(%rsp), %rax
    mov %rax, 312(%rsp)
    movl $0, 320(%rsp)
    movl $0, 324(%rsp)
    movq $0, 328(%rsp)
    movq $0, 336(%rsp)
    movq $0, 344(%rsp)
    movq $0, 352(%rsp)

    mov $455, %rax
    lea 256(%rsp), %rdi
    mov $104, %rsi
    syscall
    test %rax, %rax
    jne failed

    cmpq $2, 384(%rsp)
    jb failed
    mov 384(%rsp), %rax
    cmp %rax, 376(%rsp)
    jne failed

    /*
     * Block the main guest thread until the worker changes futex 0 -> 1 and
     * performs UMTX_OP_WAKE_PRIVATE. A five-second timeout makes regressions
     * fail deterministically instead of hanging the iPhone.
     */
    mov $454, %rax
    lea 400(%rsp), %rdi
    mov $15, %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    lea 408(%rsp), %r8
    syscall
    test %rax, %rax
    jne failed

    cmpl $1, 400(%rsp)
    jne failed
    movabs $0x4f4b444145524854, %rax /* "THREADOK" little-endian */
    cmp %rax, 368(%rsp)
    jne failed
    mov 384(%rsp), %rax
    cmp %rax, 392(%rsp)
    jne failed

    /* create -> complete -> join a logical thread */
    mov $0x4d60, %rax
    lea worker_name(%rip), %rdi
    syscall
    cmp $2, %rax
    jb failed
    mov %rax, %r14

    mov $0x4d61, %rax
    mov %r14, %rdi
    mov $17, %rsi
    syscall
    test %rax, %rax
    jne failed

    movl $0, 136(%rsp)
    mov $0x4d62, %rax
    mov %r14, %rdi
    lea 136(%rsp), %rsi
    syscall
    test %rax, %rax
    jne failed
    cmpl $17, 136(%rsp)
    jne failed

    /* recursive logical mutex lifecycle */
    mov $0x4d63, %rax
    syscall
    test %rax, %rax
    jz failed
    mov %rax, %r15

    mov $0x4d64, %rax
    mov %r15, %rdi
    syscall
    test %rax, %rax
    jne failed

    mov $0x4d64, %rax
    mov %r15, %rdi
    syscall
    test %rax, %rax
    jne failed

    mov $0x4d65, %rax
    mov %r15, %rdi
    syscall
    test %rax, %rax
    jne failed

    mov $0x4d65, %rax
    mov %r15, %rdi
    syscall
    test %rax, %rax
    jne failed

    mov $0x4d66, %rax
    mov %r15, %rdi
    syscall
    test %rax, %rax
    jne failed

    mov $4, %rax
    mov $1, %rdi
    lea ok_message(%rip), %rsi
    mov $(ok_message_end-ok_message), %rdx
    syscall

    add $512, %rsp
    mov $1, %rax
    xor %rdi, %rdi
    syscall
    ud2

failed:
    mov $4, %rax
    mov $2, %rdi
    lea fail_message(%rip), %rsi
    mov $(fail_message_end-fail_message), %rdx
    syscall
    add $512, %rsp
    mov $1, %rax
    mov $1, %rdi
    syscall
    ud2

.type worker_start,@function
worker_start:
    mov %rdi, %rbx

    /* Publish the actual TID observed inside this FEX worker context. */
    mov $432, %rax
    lea 24(%rbx), %rdi
    syscall
    test %rax, %rax
    jne .Lworker_fail

    movabs $0x4f4b444145524854, %rax /* "THREADOK" */
    mov %rax, 0(%rbx)

    /* Publish completion and wake the main FEX guest thread. */
    movl $1, 32(%rbx)
    mov $454, %rax
    lea 32(%rbx), %rdi
    mov $16, %rsi
    mov $1, %rdx
    xor %r10, %r10
    xor %r8, %r8
    syscall
    test %rax, %rax
    js .Lworker_fail

    /* End only this FreeBSD guest thread, not the whole process. */
    mov $431, %rax
    xor %rdi, %rdi
    syscall
    ud2

.Lworker_fail:
    movabs $0x4c494146524854, %rax /* "THRFAIL" */
    mov %rax, 0(%rbx)
    mov $431, %rax
    xor %rdi, %rdi
    syscall
    ud2

.section .rodata
app0_path:
    .asciz "/app0/kernel.txt"
save_path:
    .asciz "/savedata/state.bin"
worker_name:
    .asciz "worker"
save_payload:
    .ascii "state-ok\n"
ok_message:
    .ascii "guest MaxPS4Kernel services complete\n"
ok_message_end:
fail_message:
    .ascii "guest MaxPS4Kernel services failed\n"
fail_message_end:
