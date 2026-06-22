# DanyaOS Microkernel - Makefile
# Uses system GCC with -ffreestanding or i686-elf cross-compiler

CC      = gcc
AS      = nasm
LD      = ld

# Uncomment below for cross-compiler if available:
# CC  = i686-elf-gcc
# LD  = i686-elf-ld

CFLAGS  = -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
          -Isrc/include -Isrc \
          -fno-exceptions -fno-stack-protector -nostdlib \
          -m32 -march=i386 -mno-red-zone -fno-pic
ASFLAGS = -f elf32
LDFLAGS = -T linker.ld -nostdlib -m elf_i386

BUILD   = build
SRC     = src

OBJS    = $(BUILD)/kernel_entry.o \
          $(BUILD)/kernel.o \
          $(BUILD)/gdt.o \
          $(BUILD)/idt.o \
          $(BUILD)/isr.o \
          $(BUILD)/vga.o \
          $(BUILD)/keyboard.o \
          $(BUILD)/timer.o \
          $(BUILD)/pmm.o \
          $(BUILD)/vmm.o \
          $(BUILD)/heap.o \
          $(BUILD)/scheduler.o \
          $(BUILD)/ipc.o \
          $(BUILD)/syscall.o \
          $(BUILD)/tmpfs.o \
          $(BUILD)/shell.o \
          $(BUILD)/string.o

all: mkbuild $(BUILD)/danyaos.bin

mkbuild:
	@mkdir -p $(BUILD)

$(BUILD)/danyaos.bin: $(BUILD)/boot.bin $(BUILD)/kernel.bin
	cat $^ > $@
	truncate -s 1440k $@
	@echo "===== DanyaOS kernel built: $@ ====="
	@echo "Run with: qemu-system-i386 -drive format=raw,file=$(BUILD)/danyaos.bin"

$(BUILD)/boot.bin: $(SRC)/boot/boot.asm
	$(AS) -f bin $< -o $@

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	objcopy -O binary $< $@

$(BUILD)/kernel.elf: $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD)/kernel_entry.o: $(SRC)/boot/kernel_entry.asm
	@mkdir -p $(BUILD)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/isr.o: $(SRC)/kernel/isr.asm
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/%.o: $(SRC)/kernel/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/drivers/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/memory/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/process/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/syscall/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/fs/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/shell/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/libc/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BUILD)/danyaos.bin
	qemu-system-i386 -drive file=$(BUILD)/danyaos.bin,if=floppy,format=raw,index=0 -boot a

debug: $(BUILD)/danyaos.bin
	qemu-system-i386 -drive file=$(BUILD)/danyaos.bin,if=floppy,format=raw,index=0 -boot a -s -S &

clean:
	rm -rf $(BUILD)

.PHONY: all run debug clean
