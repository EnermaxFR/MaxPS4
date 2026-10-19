# Original freely reusable startup probe, no SDK/libc/imports/TLS.
# MaxPS4 STARTUP_RETURN contract: RDI -> argc/argv block, controlled RET.
.section .text
.global _start
_start:
    mov (%rdi), %eax
    add $256, %eax
    ret
    .space 16, 0x90
.section .data
    .long 0
.section .bss
    .space 64
