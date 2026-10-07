/*
 * SPDX-License-Identifier: MIT
 *
 * MaxPS4 legal typed primitive scene smoke test.
 * No Sony code, firmware, keys, SDK, or proprietary content.
 *
 * For ~5 seconds:
 *   - left stick moves a rectangle
 *   - right stick X rotates that rectangle
 *   - A changes its color
 *   - a second triangle rotates automatically
 *
 * Private MaxPS4 ABI:
 *   0x4d50 controller snapshot
 *   0x4d56 begin typed scene
 *   0x4d57 append typed primitive
 *   0x4d58 present typed scene
 *   0x4d52 frame pacing
 */

.section .text
.globl _start
.type _start,@function
_start:
    sub $160, %rsp
    xor %r12d, %r12d
    mov $300, %r13d
    xor %eax, %eax
    mov %eax, 120(%rsp)            /* auto triangle angle = 0.0 */

loop_frame:
    /* Controller snapshot at rsp+0, 28 bytes. */
    mov $0x4d50, %rax
    lea 0(%rsp), %rdi
    mov $28, %rsi
    syscall
    test %rax, %rax
    jne failed

    /* Scene begin at rsp+32, 20 bytes. */
    mov %r12d, 32(%rsp)
    movl $0x3c23d70a, 36(%rsp)      /* clear red   = 0.01 */
    movl $0x3ca3d70a, 40(%rsp)      /* clear green = 0.02 */
    movl $0x3df5c28f, 44(%rsp)      /* clear blue  = 0.12 */
    movl $0x3f800000, 48(%rsp)      /* clear alpha = 1.0 */

    mov $0x4d56, %rax
    lea 32(%rsp), %rdi
    mov $20, %rsi
    syscall
    test %rax, %rax
    jne failed

    /* Primitive #1: controller rectangle at rsp+64, 40 bytes. */
    movl $1, 64(%rsp)               /* RECT */
    mov 4(%rsp), %eax               /* left_x */
    mov %eax, 68(%rsp)
    mov 8(%rsp), %eax               /* left_y */
    mov %eax, 72(%rsp)
    movl $0x3e99999a, 76(%rsp)      /* width  = 0.30 */
    movl $0x3e4ccccd, 80(%rsp)      /* height = 0.20 */
    mov 12(%rsp), %eax              /* right_x -> rotation */
    mov %eax, 84(%rsp)

    mov 0(%rsp), %eax
    test $1, %eax
    jz rect_cyan
    movl $0x3f800000, 88(%rsp)      /* red = 1.0 */
    movl $0x3dcccccd, 92(%rsp)      /* green = 0.10 */
    movl $0x3f4ccccd, 96(%rsp)      /* blue = 0.80 */
    jmp rect_color_done
rect_cyan:
    movl $0x3dcccccd, 88(%rsp)      /* red = 0.10 */
    movl $0x3f666666, 92(%rsp)      /* green = 0.90 */
    movl $0x3f800000, 96(%rsp)      /* blue = 1.0 */
rect_color_done:
    movl $0x3f800000, 100(%rsp)     /* alpha = 1.0 */

    mov $0x4d57, %rax
    lea 64(%rsp), %rdi
    mov $40, %rsi
    syscall
    test %rax, %rax
    jne failed

    /* Primitive #2: rotating orange triangle. */
    movl $2, 64(%rsp)               /* TRIANGLE */
    movl $0x3ee66666, 68(%rsp)      /* x = +0.45 */
    movl $0xbe4ccccd, 72(%rsp)      /* y = -0.20 */
    movl $0x3eb33333, 76(%rsp)      /* width = 0.35 */
    movl $0x3eb33333, 80(%rsp)      /* height = 0.35 */
    mov 120(%rsp), %eax
    mov %eax, 84(%rsp)              /* rotation */
    movl $0x3f800000, 88(%rsp)      /* red = 1.0 */
    movl $0x3f19999a, 92(%rsp)      /* green = 0.60 */
    movl $0x3dcccccd, 96(%rsp)      /* blue = 0.10 */
    movl $0x3f800000, 100(%rsp)     /* alpha = 1.0 */

    mov $0x4d57, %rax
    lea 64(%rsp), %rdi
    mov $40, %rsi
    syscall
    test %rax, %rax
    jne failed

    mov $0x4d58, %rax
    syscall
    test %rax, %rax
    jne failed

    /* Advance triangle rotation. */
    movss 120(%rsp), %xmm0
    addss angle_step(%rip), %xmm0
    movss %xmm0, 120(%rsp)

    /* Pace to about 60 Hz. */
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

    add $160, %rsp
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
    add $160, %rsp
    mov $1, %rax
    mov $1, %rdi
    syscall
    ud2

.section .rodata
.align 4
angle_step:
    .float 0.04

ok_message:
    .ascii "guest typed scene complete\n"
ok_message_end:
fail_message:
    .ascii "guest typed scene failed\n"
fail_message_end:
