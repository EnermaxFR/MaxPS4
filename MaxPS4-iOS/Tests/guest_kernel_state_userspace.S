/*
 * SPDX-License-Identifier: MIT
 *
 * MaxPS4Kernel legal userspace smoke test.
 * No Sony code, firmware, keys, SDK, or proprietary content.
 *
 * Validates that the x86-64 guest reaches the first MaxPS4Kernel dispatcher
 * for process identity syscalls and that stdio is accepted through the
 * kernel-owned descriptor/memory tables.
 */

.section .text
.globl _start
.type _start,@function
_start:
    /* getpid -> 1 */
    mov $20, %rax
    syscall
    cmp $1, %rax
    jne failed

    /* getppid -> 0 */
    mov $39, %rax
    syscall
    test %rax, %rax
    jne failed

    /* getuid/geteuid/getegid/getgid -> 0 */
    mov $24, %rax
    syscall
    test %rax, %rax
    jne failed

    mov $25, %rax
    syscall
    test %rax, %rax
    jne failed

    mov $43, %rax
    syscall
    test %rax, %rax
    jne failed

    mov $47, %rax
    syscall
    test %rax, %rax
    jne failed

    /* write via kernel-owned stdout descriptor validation */
    mov $4, %rax
    mov $1, %rdi
    lea ok_message(%rip), %rsi
    mov $(ok_message_end-ok_message), %rdx
    syscall

    /* exit is dispatched by MaxPS4Kernel. */
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
    mov $1, %rax
    mov $1, %rdi
    syscall
    ud2

.section .rodata
ok_message:
    .ascii "guest MaxPS4Kernel state complete\n"
ok_message_end:
fail_message:
    .ascii "guest MaxPS4Kernel state failed\n"
fail_message_end:
