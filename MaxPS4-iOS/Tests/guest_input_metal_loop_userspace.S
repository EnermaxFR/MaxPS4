/*
 * SPDX-License-Identifier: MIT
 *
 * MaxPS4 legal realtime input -> Metal smoke test.
 * No Sony code, firmware, keys, SDK, or proprietary content.
 *
 * For ~10 seconds:
 *   0x4d50: read controller snapshot
 *   0x4d51: submit square render command
 *   0x4d52: pace one frame (~16.7 ms)
 */

.section .text
.globl _start
.type _start,@function
_start:
    sub $96, %rsp
    xor %r12d, %r12d
    mov $600, %r13d

loop_frame:
    /* controller snapshot at rsp+0, 28 bytes */
    mov $0x4d50, %rax
    lea 0(%rsp), %rdi
    mov $28, %rsi
    syscall
    test %rax, %rax
    jne failed

    /* render command at rsp+32, 32 bytes */
    mov %r12d, 32(%rsp)            /* sequence */
    mov 4(%rsp), %eax              /* left_x */
    mov %eax, 36(%rsp)
    mov 8(%rsp), %eax              /* left_y */
    mov %eax, 40(%rsp)
    movl $0x3f800000, 44(%rsp)      /* scale = 1.0 */

    /* A button -> magenta, otherwise cyan */
    mov 0(%rsp), %eax
    test $1, %eax
    jz color_cyan
    movl $0x3f800000, 48(%rsp)      /* red = 1.0 */
    movl $0x3e19999a, 52(%rsp)      /* green = 0.15 */
    movl $0x3f59999a, 56(%rsp)      /* blue = 0.85 */
    jmp color_done
color_cyan:
    movl $0x3e19999a, 48(%rsp)      /* red = 0.15 */
    movl $0x3f733333, 52(%rsp)      /* green = 0.95 */
    movl $0x3f800000, 56(%rsp)      /* blue = 1.0 */
color_done:
    movl $0x3f800000, 60(%rsp)      /* alpha = 1.0 */

    mov $0x4d51, %rax
    lea 32(%rsp), %rdi
    mov $32, %rsi
    syscall
    test %rax, %rax
    jne failed

    /* frame pacing */
    mov $0x4d52, %rax
    mov $16667, %rdi
    syscall
    test %rax, %rax
    jne failed

    inc %r12d
    dec %r13d
    jnz loop_frame

    mov $4, %rax
    mov $1, %rdi
    lea ok_message(%rip), %rsi
    mov $(ok_message_end-ok_message), %rdx
    syscall

    add $96, %rsp
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
    add $96, %rsp
    mov $1, %rax
    mov $1, %rdi
    syscall
    ud2

.section .rodata
ok_message:
    .ascii "guest input metal loop complete\n"
ok_message_end:
fail_message:
    .ascii "guest input metal loop failed\n"
fail_message_end:
