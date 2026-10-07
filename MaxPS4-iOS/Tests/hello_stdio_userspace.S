/*
 * SPDX-License-Identifier: MIT
 *
 * MaxPS4 legal userspace smoke test.
 * No Sony code, firmware, keys, SDK, or proprietary content.
 *
 * Exercises only the small userspace syscall bridge used by MaxPS4:
 * write(4), read(3), exit(1).
 */

.section .text
.globl _start
.type _start,@function
_start:
    /* write(1, "enter name\n", 11) */
    mov $4, %rax
    mov $1, %rdi
    lea prompt(%rip), %rsi
    mov $(prompt_end-prompt), %rdx
    syscall

    /*
     * Build one contiguous response buffer:
     *   "Hello, " + bytes returned by read()
     * This keeps the post-read output to a single write syscall.
     */
    sub $272, %rsp
    movabs $0x00202c6f6c6c6548, %rax
    mov %rax, (%rsp)

    /* read(0, rsp+7, 248) */
    mov $3, %rax
    xor %rdi, %rdi
    lea 7(%rsp), %rsi
    mov $248, %rdx
    syscall
    mov %rax, %r12

    /* write(1, buffer, 7 + bytes_read) */
    test %r12, %r12
    jle 1f
    add $7, %r12
    mov $4, %rax
    mov $1, %rdi
    mov %rsp, %rsi
    mov %r12, %rdx
    syscall

1:
    add $272, %rsp

    /* exit(0) */
    mov $1, %rax
    xor %rdi, %rdi
    syscall

    ud2

.section .rodata
prompt:
    .ascii "enter name\n"
prompt_end:
