/*
 * SPDX-License-Identifier: MIT
 *
 * MaxPS4 legal guest-owned RGBA texture upload smoke test.
 * No Sony code, firmware, keys, SDK, or proprietary content.
 *
 * Uploads a 4x4 RGBA8 texture from x86-64 guest memory, then renders it
 * for ~5 seconds through the typed scene command buffer.
 *
 * Private MaxPS4 ABI:
 *   0x4d50 controller snapshot
 *   0x4d52 frame pacing
 *   0x4d56 begin typed scene
 *   0x4d58 present typed scene
 *   0x4d59 upload RGBA8 texture
 *   0x4d5a append guest-textured primitive v2
 */

.section .text
.globl _start
.type _start,@function
_start:
    sub $176, %rsp

    /* Upload guest-owned 4x4 RGBA8 texture, id=7. */
    mov $0x4d59, %rax
    mov $7, %rdi
    mov $4, %rsi
    mov $4, %rdx
    lea guest_pixels(%rip), %r10
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
    movl $0x3b83126f, 36(%rsp)      /* clear r ~= 0.004 */
    movl $0x3ca3d70a, 40(%rsp)      /* clear g = 0.02 */
    movl $0x3df5c28f, 44(%rsp)      /* clear b = 0.12 */
    movl $0x3f800000, 48(%rsp)      /* clear a = 1.0 */

    mov $0x4d56, %rax
    lea 32(%rsp), %rdi
    mov $20, %rsi
    syscall
    test %rax, %rax
    jne failed

    /* Primitive v2 at rsp+64, 44 bytes. */
    movl $4, 64(%rsp)               /* GUEST_TEXTURED_QUAD */
    mov 4(%rsp), %eax               /* x = left_x */
    mov %eax, 68(%rsp)
    mov 8(%rsp), %eax               /* y = left_y */
    mov %eax, 72(%rsp)
    movl $0x3f0ccccd, 76(%rsp)      /* width  = 0.55 */
    movl $0x3eb33333, 80(%rsp)      /* height = 0.35 */
    mov 12(%rsp), %eax              /* rotation = right_x */
    mov %eax, 84(%rsp)
    movl $0x3f800000, 88(%rsp)      /* color fields reserved */
    movl $0x3f800000, 92(%rsp)
    movl $0x3f800000, 96(%rsp)
    movl $0x3f800000, 100(%rsp)
    movl $7, 104(%rsp)              /* texture_id */

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

    add $176, %rsp
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
    add $176, %rsp
    mov $1, %rax
    mov $1, %rdi
    syscall
    ud2

.section .rodata
.align 4
guest_pixels:
    /* Row 0 */
    .byte 255, 48, 48, 255,   255, 180, 40, 255,   40, 220, 255, 255,   90, 70, 255, 255
    /* Row 1 */
    .byte 40, 220, 255, 255,  90, 70, 255, 255,    255, 48, 48, 255,    255, 180, 40, 255
    /* Row 2 */
    .byte 255, 180, 40, 255,  255, 48, 48, 255,    90, 70, 255, 255,    40, 220, 255, 255
    /* Row 3 */
    .byte 90, 70, 255, 255,   40, 220, 255, 255,   255, 180, 40, 255,   255, 48, 48, 255

ok_message:
    .ascii "guest texture upload complete\n"
ok_message_end:
fail_message:
    .ascii "guest texture upload failed\n"
fail_message_end:
