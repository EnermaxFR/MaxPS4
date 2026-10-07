/*
 * SPDX-License-Identifier: MIT
 *
 * MaxPS4 legal atomic frame-queue smoke test.
 * No Sony code, firmware, keys, SDK, or proprietary content.
 *
 * Runs ~5 seconds:
 *   0x4d50 controller snapshot
 *   0x4d53 begin frame
 *   0x4d54 submit rectangle (3x)
 *   0x4d55 present frame
 *   0x4d52 frame pacing
 */

.section .text
.globl _start
.type _start,@function
_start:
    sub $128, %rsp
    xor %r12d, %r12d
    mov $300, %r13d

loop_frame:
    /* Controller snapshot: rsp+0, 28 bytes. */
    mov $0x4d50, %rax
    lea 0(%rsp), %rdi
    mov $28, %rsi
    syscall
    test %rax, %rax
    jne failed

    /* Begin frame: sequence + RGBA clear, rsp+32, 20 bytes. */
    mov %r12d, 32(%rsp)
    movl $0x3c23d70a, 36(%rsp)     /* clear r = 0.01 */
    movl $0x3d4ccccd, 40(%rsp)     /* clear g = 0.05 */
    movl $0x3df5c28f, 44(%rsp)     /* clear b = 0.12 */
    movl $0x3f800000, 48(%rsp)     /* clear a = 1.0 */
    mov $0x4d53, %rax
    lea 32(%rsp), %rdi
    mov $20, %rsi
    syscall
    test %rax, %rax
    jne failed

    /* Rect #1 follows left stick, rsp+64, 28 bytes. */
    mov 4(%rsp), %eax
    mov %eax, 64(%rsp)             /* x = left_x */
    mov 8(%rsp), %eax
    mov %eax, 68(%rsp)             /* y = left_y */
    movl $0x3f4ccccd, 72(%rsp)     /* scale = 0.8 */

    /* A button switches moving rect from cyan to magenta. */
    mov 0(%rsp), %eax
    test $1, %eax
    jz moving_cyan
    movl $0x3f800000, 76(%rsp)
    movl $0x3dcccccd, 80(%rsp)
    movl $0x3f4ccccd, 84(%rsp)
    jmp moving_color_done
moving_cyan:
    movl $0x3dcccccd, 76(%rsp)
    movl $0x3f666666, 80(%rsp)
    movl $0x3f800000, 84(%rsp)
moving_color_done:
    movl $0x3f800000, 88(%rsp)

    mov $0x4d54, %rax
    lea 64(%rsp), %rdi
    mov $28, %rsi
    syscall
    test %rax, %rax
    jne failed

    /* Rect #2 fixed upper-left, yellow. */
    movl $0xbf0ccccd, 64(%rsp)     /* x = -0.55 */
    movl $0x3f0ccccd, 68(%rsp)     /* y = +0.55 */
    movl $0x3f0ccccd, 72(%rsp)     /* scale = 0.55 */
    movl $0x3f800000, 76(%rsp)
    movl $0x3f400000, 80(%rsp)
    movl $0x3dcccccd, 84(%rsp)
    movl $0x3f800000, 88(%rsp)

    mov $0x4d54, %rax
    lea 64(%rsp), %rdi
    mov $28, %rsi
    syscall
    test %rax, %rax
    jne failed

    /* Rect #3 fixed lower-right, purple. */
    movl $0x3f0ccccd, 64(%rsp)     /* x = +0.55 */
    movl $0xbf0ccccd, 68(%rsp)     /* y = -0.55 */
    movl $0x3ee66666, 72(%rsp)     /* scale = 0.45 */
    movl $0x3f0ccccd, 76(%rsp)
    movl $0x3e4ccccd, 80(%rsp)
    movl $0x3f800000, 84(%rsp)
    movl $0x3f800000, 88(%rsp)

    mov $0x4d54, %rax
    lea 64(%rsp), %rdi
    mov $28, %rsi
    syscall
    test %rax, %rax
    jne failed

    /* Atomically publish the completed frame. */
    mov $0x4d55, %rax
    syscall
    test %rax, %rax
    jne failed

    /* Pace to roughly 60 Hz. */
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

    add $128, %rsp
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
    add $128, %rsp
    mov $1, %rax
    mov $1, %rdi
    syscall
    ud2

.section .rodata
ok_message:
    .ascii "guest frame queue complete\n"
ok_message_end:
fail_message:
    .ascii "guest frame queue failed\n"
fail_message_end:
