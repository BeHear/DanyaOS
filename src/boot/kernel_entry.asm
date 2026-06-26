; DOSFS Microkernel - Kernel Entry Point (Multiboot2)

[BITS 32]

[SECTION .text]
[GLOBAL _start]
[EXTERN kernel_main]

; Multiboot2 header - no video mode request
align 8
mb2_start:
    dd 0xE85250D6                       ; magic
    dd 0                                 ; architecture (i386)
    dd mb2_end - mb2_start              ; header length
    dd -(0xE85250D6 + 0 + (mb2_end - mb2_start))

    ; End tag
    dw 0                                 ; type
    dw 0                                 ; flags
    dd 8                                 ; size
mb2_end:

_start:
    mov [mb_magic_save], eax
    mov [mb_info_save], ebx
    mov esp, 0x90000
    push dword [mb_info_save]
    push dword [mb_magic_save]
    call kernel_main
    cli
.hang:
    hlt
    jmp .hang

[SECTION .data]
mb_magic_save: dd 0
mb_info_save:  dd 0
