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

    /* read(0, rsp_buffer, 255) */
    sub $256, %rsp
    mov $3, %rax
    xor %rdi, %rdi
    mov %rsp, %rsi
    mov $255, %rdx
    syscall
    mov %rax, %r12

    /* write(1, "Hello, ", 7) */
    mov $4, %rax
    mov $1, %rdi
    lea hello(%rip), %rsi
    mov $(hello_end-hello), %rdx
    syscall

    /* Echo the bytes returned by read(). */
    test %r12, %r12
    jle 1f
    mov $4, %rax
    mov $1, %rdi
    mov %rsp, %rsi
    mov %r12, %rdx
    syscall

1:
    add $256, %rsp

    /* exit(0) */
    mov $1, %rax
    xor %rdi, %rdi
    syscall

    /* The bridge treats exit as non-returning. */
    ud2

.section .rodata
prompt:
    .ascii "enter name\n"
prompt_end:
hello:
    .ascii "Hello, "
hello_end:
