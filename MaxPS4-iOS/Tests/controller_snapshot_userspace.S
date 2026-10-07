/*
 * SPDX-License-Identifier: MIT
 *
 * MaxPS4 legal controller snapshot smoke test.
 * No Sony code, firmware, keys, SDK, or proprietary content.
 *
 * Exercises only MaxPS4's private userspace controller pseudo-syscall:
 *   0x4d50(snapshot*, 28)
 * plus write(4) and exit(1).
 */

.section .text
.globl _start
.type _start,@function

_start:
    sub $256, %rsp

    /* Ask MaxPS4/FEX for one controller snapshot (28 bytes). */
    mov $0x4d50, %rax
    lea 0(%rsp), %rdi
    mov $28, %rsi
    syscall
    test %rax, %rax
    jne snapshot_fail

    /* write(1, "controller buttons=0x", ...) */
    mov $4, %rax
    mov $1, %rdi
    lea buttons_label(%rip), %rsi
    mov $(buttons_label_end-buttons_label), %rdx
    syscall

    /* Convert 32-bit button mask to 8 hexadecimal digits. */
    mov 0(%rsp), %eax
    lea 64(%rsp), %rdi
    mov $8, %ecx
1:
    rol $4, %eax
    mov %eax, %edx
    and $0xf, %edx
    lea hex_digits(%rip), %rsi
    movzbl (%rsi,%rdx,1), %edx
    mov %dl, (%rdi)
    inc %rdi
    loop 1b
    movb $'\n', (%rdi)

    mov $4, %rax
    mov $1, %rdi
    lea 64(%rsp), %rsi
    mov $9, %rdx
    syscall

    /* Copy the axis/trigger template to rsp+96. */
    lea state_template(%rip), %rsi
    lea 96(%rsp), %rdi
    mov $(state_template_end-state_template), %rcx
    rep movsb

    /* Signed stick values, expressed as integer percent (-100..100). */
    movss hundred(%rip), %xmm7

    movss 4(%rsp), %xmm0
    mulss %xmm7, %xmm0
    cvttss2si %xmm0, %eax
    lea 98(%rsp), %rdi
    call put_signed3

    movss 8(%rsp), %xmm0
    mulss %xmm7, %xmm0
    cvttss2si %xmm0, %eax
    lea 103(%rsp), %rdi
    call put_signed3

    movss 12(%rsp), %xmm0
    mulss %xmm7, %xmm0
    cvttss2si %xmm0, %eax
    lea 110(%rsp), %rdi
    call put_signed3

    movss 16(%rsp), %xmm0
    mulss %xmm7, %xmm0
    cvttss2si %xmm0, %eax
    lea 115(%rsp), %rdi
    call put_signed3

    /* Unsigned trigger values, expressed as integer percent (0..100). */
    movss 20(%rsp), %xmm0
    mulss %xmm7, %xmm0
    cvttss2si %xmm0, %eax
    lea 123(%rsp), %rdi
    call put_unsigned3

    movss 24(%rsp), %xmm0
    mulss %xmm7, %xmm0
    cvttss2si %xmm0, %eax
    lea 130(%rsp), %rdi
    call put_unsigned3

    mov $4, %rax
    mov $1, %rdi
    lea 96(%rsp), %rsi
    mov $(state_template_end-state_template), %rdx
    syscall

    add $256, %rsp
    mov $1, %rax
    xor %rdi, %rdi
    syscall
    ud2

snapshot_fail:
    mov $4, %rax
    mov $1, %rdi
    lea fail_message(%rip), %rsi
    mov $(fail_message_end-fail_message), %rdx
    syscall
    add $256, %rsp
    mov $1, %rax
    mov $1, %rdi
    syscall
    ud2

/* eax = -100..100, rdi = destination[4], writes sign + 3 digits. */
put_signed3:
    push %rbx
    test %eax, %eax
    jge 2f
    movb $'-', (%rdi)
    neg %eax
    jmp 3f
2:
    movb $'+', (%rdi)
3:
    inc %rdi
    call put_unsigned3
    pop %rbx
    ret

/* eax = 0..999, rdi = destination[3], writes three decimal digits. */
put_unsigned3:
    push %rbx
    push %rcx
    push %rdx
    cmp $999, %eax
    jle 4f
    mov $999, %eax
4:
    xor %edx, %edx
    mov $100, %ecx
    div %ecx
    add $'0', %al
    mov %al, (%rdi)
    mov %edx, %eax
    xor %edx, %edx
    mov $10, %ecx
    div %ecx
    add $'0', %al
    add $'0', %dl
    mov %al, 1(%rdi)
    mov %dl, 2(%rdi)
    pop %rdx
    pop %rcx
    pop %rbx
    ret

.section .rodata
hex_digits:
    .ascii "0123456789ABCDEF"
buttons_label:
    .ascii "controller buttons=0x"
buttons_label_end:
state_template:
    .ascii "L +000/+000 R +000/+000 LT 000 RT 000\n"
state_template_end:
fail_message:
    .ascii "controller snapshot failed\n"
fail_message_end:
hundred:
    .float 100.0
