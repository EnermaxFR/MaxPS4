.section .text
.global _start
.type _start,@function
_start:
    mov $4, %rax
    mov $1, %rdi
    lea message(%rip), %rsi
    mov $(message_end-message), %rdx
    syscall
    hlt
.size _start, .-_start

.section .rodata
message:
    .ascii "MaxPS4 FEX minimal guest OK\n"
message_end:
