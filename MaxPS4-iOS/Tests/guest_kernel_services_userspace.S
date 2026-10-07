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
 */

.section .text
.globl _start
.type _start,@function
_start:
    sub $256, %rsp

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

    add $256, %rsp
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
    add $256, %rsp
    mov $1, %rax
    mov $1, %rdi
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
