# Original freestanding test application. No SDK, libc, imports or DRM.
# Experimental MaxPS4 bounded-HLT contract, NOT the PS4 process startup ABI.
.section .text
.global _start
_start:
    movabs $answer, %rdi
    mov (%rdi), %eax
    add $256, %eax
    hlt
.section .data
.balign 4
answer:
    .long 40
.section .bss
    .space 64
