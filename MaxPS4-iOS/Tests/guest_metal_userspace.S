/*
 * SPDX-License-Identifier: MIT
 *
 * MaxPS4 legal guest -> Metal render smoke test.
 * No Sony code, firmware, keys, SDK, or proprietary content.
 *
 * Sends one private MaxPS4 square-render command through FEX using
 * pseudo-syscall 0x4d51, then writes a confirmation line and exits.
 */

.section .text
.globl _start
.type _start,@function
_start:
    sub $64, %rsp

    /* MaxPS4GuestRenderState, 32 bytes */
    movl $1, 0(%rsp)               /* sequence */
    movl $0x3eb33333, 4(%rsp)      /* x = 0.35 */
    movl $0xbe800000, 8(%rsp)      /* y = -0.25 */
    movl $0x3f933333, 12(%rsp)     /* scale = 1.15 */
    movl $0x3f800000, 16(%rsp)     /* red = 1.0 */
    movl $0x3e19999a, 20(%rsp)     /* green = 0.15 */
    movl $0x3f59999a, 24(%rsp)     /* blue = 0.85 */
    movl $0x3f800000, 28(%rsp)     /* alpha = 1.0 */

    mov $0x4d51, %rax
    mov %rsp, %rdi
    mov $32, %rsi
    syscall
    test %rax, %rax
    jne failed

    mov $4, %rax
    mov $1, %rdi
    lea ok_message(%rip), %rsi
    mov $(ok_message_end-ok_message), %rdx
    syscall

    add $64, %rsp
    mov $1, %rax
    xor %rdi, %rdi
    syscall
    ud2

failed:
    mov $4, %rax
    mov $1, %rdi
    lea fail_message(%rip), %rsi
    mov $(fail_message_end-fail_message), %rdx
    syscall
    add $64, %rsp
    mov $1, %rax
    mov $1, %rdi
    syscall
    ud2

.section .rodata
ok_message:
    .ascii "guest metal command sent\n"
ok_message_end:
fail_message:
    .ascii "guest metal command failed\n"
fail_message_end:
