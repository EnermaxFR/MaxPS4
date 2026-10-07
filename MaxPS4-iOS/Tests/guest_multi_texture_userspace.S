/*
 * SPDX-License-Identifier: MIT
 *
 * MaxPS4 legal multiple guest texture resource smoke test.
 * No Sony code, firmware, keys, SDK, or proprietary content.
 *
 * Uploads two independent 4x4 RGBA8 textures from x86-64 guest memory and
 * renders both in the same typed scene for ~5 seconds.
 *
 * Controls:
 *   - left stick moves texture id 7
 *   - right stick X rotates texture id 11
 */

.section .text
.globl _start
.type _start,@function
_start:
    sub $192, %rsp

    /* Upload texture id 7. */
    mov $0x4d59, %rax
    mov $7, %rdi
    mov $4, %rsi
    mov $4, %rdx
    lea guest_pixels_a(%rip), %r10
    mov $64, %r8
    syscall
    test %rax, %rax
    jne failed

    /* Upload texture id 11. */
    mov $0x4d59, %rax
    mov $11, %rdi
    mov $4, %rsi
    mov $4, %rdx
    lea guest_pixels_b(%rip), %r10
    mov $64, %r8
    syscall
    test %rax, %rax
    jne failed

    xor %r12d, %r12d
    mov $300, %r13d

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
    movl $0x3b83126f, 36(%rsp)
    movl $0x3c23d70a, 40(%rsp)
    movl $0x3d4ccccd, 44(%rsp)
    movl $0x3f800000, 48(%rsp)

    mov $0x4d56, %rax
    lea 32(%rsp), %rdi
    mov $20, %rsi
    syscall
    test %rax, %rax
    jne failed

    /* Primitive #1: texture id 7, controlled by left stick. */
    movl $4, 64(%rsp)
    mov 4(%rsp), %eax
    mov %eax, 68(%rsp)
    mov 8(%rsp), %eax
    mov %eax, 72(%rsp)
    movl $0x3ef5c28f, 76(%rsp)      /* width 0.48 */
    movl $0x3e99999a, 80(%rsp)      /* height 0.30 */
    movl $0x00000000, 84(%rsp)
    movl $0x3f800000, 88(%rsp)
    movl $0x3f800000, 92(%rsp)
    movl $0x3f800000, 96(%rsp)
    movl $0x3f800000, 100(%rsp)
    movl $7, 104(%rsp)

    mov $0x4d5a, %rax
    lea 64(%rsp), %rdi
    mov $44, %rsi
    syscall
    test %rax, %rax
    jne failed

    /* Primitive #2: texture id 11, fixed right side, right-X rotation. */
    movl $4, 64(%rsp)
    movl $0x3f19999a, 68(%rsp)      /* x = +0.60 */
    movl $0xbe4ccccd, 72(%rsp)      /* y = -0.20 */
    movl $0x3ef5c28f, 76(%rsp)      /* width 0.48 */
    movl $0x3e99999a, 80(%rsp)      /* height 0.30 */
    mov 12(%rsp), %eax
    mov %eax, 84(%rsp)
    movl $0x3f800000, 88(%rsp)
    movl $0x3f800000, 92(%rsp)
    movl $0x3f800000, 96(%rsp)
    movl $0x3f800000, 100(%rsp)
    movl $11, 104(%rsp)

    mov $0x4d5a, %rax
    lea 64(%rsp), %rdi
    mov $44, %rsi
    syscall
    test %rax, %rax
    jne failed

    mov $0x4d58, %rax
    syscall
    test %rax, %rax
    jne failed

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

    add $192, %rsp
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
    add $192, %rsp
    mov $1, %rax
    mov $1, %rdi
    syscall
    ud2

.section .rodata
.align 4
guest_pixels_a:
    /* warm/cyan checker */
    .byte 255, 55, 70, 255,   255, 190, 35, 255,   35, 220, 255, 255,   255, 55, 70, 255
    .byte 35, 220, 255, 255,  255, 55, 70, 255,    255, 190, 35, 255,   35, 220, 255, 255
    .byte 255, 190, 35, 255,  35, 220, 255, 255,   255, 55, 70, 255,    255, 190, 35, 255
    .byte 255, 55, 70, 255,   255, 190, 35, 255,   35, 220, 255, 255,   255, 55, 70, 255

guest_pixels_b:
    /* green/violet checker */
    .byte 40, 255, 100, 255,  120, 70, 255, 255,   40, 255, 100, 255,  120, 70, 255, 255
    .byte 120, 70, 255, 255,  40, 255, 100, 255,   120, 70, 255, 255,  40, 255, 100, 255
    .byte 40, 255, 100, 255,  120, 70, 255, 255,   40, 255, 100, 255,  120, 70, 255, 255
    .byte 120, 70, 255, 255,  40, 255, 100, 255,   120, 70, 255, 255,  40, 255, 100, 255

ok_message:
    .ascii "guest multi texture complete\n"
ok_message_end:
fail_message:
    .ascii "guest multi texture failed\n"
fail_message_end:
