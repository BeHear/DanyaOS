# DanyaOS v1.3.5

A hobby microkernel operating system written in C, Rust, and x86 assembly.

## Architecture

- **Microkernel design** - minimal kernel with userspace drivers
- **x86 (i386)** target architecture
- **GRUB/Multiboot** bootloader — boots from USB, CD/DVD, HDD
- **Preemptive multitasking** scheduler
- **Virtual memory** with paging
- **IPC** (Inter-Process Communication)
- **System calls** via INT 0x80
- **tmpfs** in-memory filesystem
- **FAT16** disk filesystem support
- **ATA/IDE** disk driver (PIO mode)
- **ACPI** power management (on supported hardware)
- **Interactive shell** with built-in commands
- **TUI** (Text User Interface) with menu system

## What's New in v1.3

- **GRUB/Multiboot bootloader** — boot from USB, CD/DVD, HDD on any x86 PC
- **ATA/IDE PIO driver** — real disk access for reading/writing data
- **FAT16 filesystem** — persistent file storage on disk
- **ACPI support** — shutdown/reboot on supported hardware
- **Multiboot info** — memory detection via bootloader
- **New shell commands**: disk, fatls, fatread, fatwrite

## Building

### Prerequisites

- `gcc` (system compiler with `-ffreestanding` support)
- `nasm` assembler
- `grub-mkrescue` (for ISO generation)
- `qemu-system-i386` (for testing)
- `xorriso`, `mtools` (for GRUB ISO creation)

### Install dependencies

**Debian/Ubuntu:**
```bash
sudo apt install gcc nasm qemu-system-x86 grub-common xorriso mtools
```

**Arch Linux:**
```bash
sudo pacman -S gcc nasm qemu-full grub xorriso mtools
```

### Build

```bash
make
```

### Run in QEMU

```bash
make qemu
```

### Run with USB passthrough

```bash
make qemu-usb
```

### Create bootable USB

```bash
make
sudo dd if=build/danyaos.iso of=/dev/sdX bs=4M status=progress
```

## Shell Commands

```
help              - show available commands
clear/cls         - clear screen
echo <msg>        - print message
uname             - system info
mem/free          - memory info
uptime            - timer ticks
ps                - list processes
create <name>     - create process
ipc               - test IPC
ls                - list files (tmpfs)
touch <file>      - create file (tmpfs)
write <file> <data> - write to file (tmpfs)
cat <file>        - read file (tmpfs)
rm <file>         - delete file (tmpfs)
cp <src> <dst>    - copy file (tmpfs)
mv <src> <dst>    - move/rename file (tmpfs)
hexdump <file>    - hex dump of file
color <fg> <bg>   - set terminal colors
date              - system uptime
whoami            - current user
pwd               - current directory
calc <a> <op> <b> - calculator (+ - * / %%)
history           - command history
reset             - reset terminal
beep              - PC speaker beep
about             - about DanyaOS
tuitest           - TUI demo
shutdown          - ACPI shutdown / halt
reboot            - ACPI reboot
cpuinfo           - CPU information (vendor, brand, features)
disk              - ATA disk information
fatls             - list FAT16 files
fatread <file>    - read FAT16 file
fatwrite <file> <data> - write FAT16 file
sacpi <args>      - simulated ACPI console
```

## Running on Real Hardware

### Requirements

- x86 (i386/i686) compatible processor
- BIOS or UEFI with Legacy/CSM boot support
- PS/2 keyboard (USB keyboard may work with some BIOS)
- VGA text-mode display

### Boot from USB

1. Build: `make`
2. Write to USB: `sudo dd if=build/danyaos.iso of=/dev/sdX bs=4M status=progress`
3. Boot from USB in BIOS (Legacy/CSM mode)

### Boot from CD/DVD

1. Build: `make`
2. Burn `build/danyaos.iso` to CD/DVD
3. Boot from CD/DVD

### Boot from HDD (dual-boot)

1. Build: `make`
2. Copy `build/danyaos.iso` to a partition accessible by GRUB
3. Add a GRUB menuentry for DanyaOS

## Project Structure

```
DanyaOS/
├── Makefile
├── grub.cfg
├── linker.ld
├── src/
│   ├── boot/           # Multiboot entry point
│   ├── kernel/         # Core kernel (GDT, IDT, ISR, kernel_main)
│   ├── drivers/        # Device drivers (VGA, keyboard, timer, ATA, ACPI)
│   ├── memory/         # Memory management (PMM, VMM, heap)
│   ├── process/        # Process management & scheduler, IPC
│   ├── syscall/        # System call interface
│   ├── fs/             # Filesystem (tmpfs, FAT16)
│   ├── shell/          # Interactive shell
│   ├── tui/            # Text user interface
│   ├── libc/           # Minimal C library
│   ├── include/        # Common headers (types, I/O, multiboot)
│   └── rust/           # Rust kernel modules (PMM, IPC)
└── build/              # Build output
```

## Syscall Table

| # | Name | Arguments |
|---|------|-----------|
| 1 | write | EBX=buf, ECX=len |
| 2 | read | EBX=buf, ECX=max |
| 3 | exit | EBX=code |
| 6 | getpid | - |
| 7 | ipc_send | EBX=to, ECX=data, EDX=len |
| 8 | ipc_recv | EBX=from, ECX=buf, EDX=max |
| 9 | sleep | - |
| 10 | meminfo | returns free/total in EAX/EBX |

## License

MIT License

## Author

DanyaOS Project (2025)
