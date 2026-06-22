; DanyaOS Microkernel - Bootloader (Stage 1)
; Loads kernel from disk and jumps to protected mode

[BITS 16]
[ORG 0x7C00]

KERNEL_OFFSET equ 0x1000
SECTORS_TO_READ equ 40

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [BOOT_DRIVE], dl

    mov si, msg_boot
    call print_string_rm

    call load_kernel
    call switch_to_pm
    jmp $

print_string_rm:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print_string_rm
.done:
    ret

load_kernel:
    mov si, msg_load
    call print_string_rm

    mov bx, KERNEL_OFFSET
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, [BOOT_DRIVE]
    mov ah, 0x02
    mov al, SECTORS_TO_READ
    int 0x13
    jc disk_error
    ret

disk_error:
    mov si, msg_disk_err
    call print_string_rm
    jmp $

gdt_start:
    dq 0x0

gdt_code:
    dw 0xFFFF
    dw 0x0
    db 0x0
    db 10011010b
    db 11001111b
    db 0x0

gdt_data:
    dw 0xFFFF
    dw 0x0
    db 0x0
    db 10010010b
    db 11001111b
    db 0x0

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

switch_to_pm:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp CODE_SEG:init_pm

[BITS 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ebp, 0x90000
    mov esp, ebp
    jmp KERNEL_OFFSET

BOOT_DRIVE: db 0
msg_boot:   db "DanyaOS v1.1.1 booting...", 13, 10, 0
msg_load:   db "Loading kernel...", 13, 10, 0
msg_disk_err: db "Disk read error!", 13, 10, 0

times 510-($-$$) db 0
dw 0xAA55
