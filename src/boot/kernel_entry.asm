; DanyaOS Microkernel - Kernel Entry Point
; Sets up stack, zeroes BSS, calls kernel_main

[BITS 32]
[EXTERN kernel_main]
[EXTERN __bss_start]
[EXTERN __bss_end]

[GLOBAL _start]
[SECTION .text]

_start:
    mov esp, 0x90000

    ; Zero BSS section
    mov edi, __bss_start
    mov esi, __bss_end
    mov ecx, esi
    sub ecx, edi
    shr ecx, 2
    xor eax, eax
    rep stosd

    call kernel_main
    cli
.hang:
    hlt
    jmp .hang
