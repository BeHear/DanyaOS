# DOSFS Microkernel - Makefile
# GRUB/Multiboot-based build for real hardware boot

CC      = gcc
AS      = nasm
LD      = ld

CFLAGS  = -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
          -Isrc/include -Isrc \
          -fno-exceptions -fno-stack-protector -nostdlib \
          -m32 -march=i386 -mno-red-zone -fno-pic
ASFLAGS = -f elf32
LDFLAGS = -T linker.ld -nostdlib -m elf_i386

BUILD   = build
SRC     = src
RUST_TARGET = target-specs/i686-unknown-none.json

RUST_TARGET_NAME = $(notdir $(basename $(RUST_TARGET)))
RUST_LIB = rust/target/$(RUST_TARGET_NAME)/release/libdosfs_kernel.a

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
          $(BUILD)/fat16.o \
          $(BUILD)/ata.o \
           $(BUILD)/rtc.o \
           $(BUILD)/pci.o \
           $(BUILD)/acpi.o \
           $(BUILD)/acpi_sim.o \
           $(BUILD)/ne2000.o \
           $(BUILD)/net.o \
          $(BUILD)/cpu_sim.o \
          $(BUILD)/editor.o \
          $(BUILD)/shell.o \
          $(BUILD)/cpuinfo.o \
          $(BUILD)/tui.o \
          $(BUILD)/string.o

all: mkbuild rust-lib $(BUILD)/dosfs.iso

kernel: mkbuild rust-lib $(BUILD)/kernel.elf

mkbuild:
	@mkdir -p $(BUILD)

rust-lib:
	@echo "===== Building Rust kernel modules ====="
	cd rust && cargo +nightly build -Zjson-target-spec -Zbuild-std=core \
		--target ../$(RUST_TARGET) --release
	@echo "===== Rust modules built ====="

$(BUILD)/kernel.elf: $(OBJS) $(RUST_LIB)
	$(LD) $(LDFLAGS) $(OBJS) $(RUST_LIB) -o $@
	@echo "===== Kernel ELF built: $@ ====="

$(BUILD)/dosfs.iso: $(BUILD)/kernel.elf
	@mkdir -p $(BUILD)/isodir/boot/grub
	cp $(BUILD)/kernel.elf $(BUILD)/isodir/boot/dosfs.elf
	cp grub.cfg $(BUILD)/isodir/boot/grub/grub.cfg
	grub-mkrescue -o $(BUILD)/dosfs.iso $(BUILD)/isodir 2>/dev/null
	@echo "===== ISO built: $@ ====="
	@echo "BIOS:   qemu-system-i386 -cdrom $(BUILD)/dosfs.iso -m 256M"
	@echo "UEFI:   qemu-system-x86_64 -bios /usr/share/edk2/x64/OVMF.4m.fd -cdrom $(BUILD)/dosfs.iso -m 256M"
	@echo "USB:    sudo dd if=$(BUILD)/dosfs.iso of=/dev/sdX bs=4M status=progress"

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

$(BUILD)/%.o: $(SRC)/tui/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/tools/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/libc/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/net/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

QEMU_NET = -nic model=ne2k_isa

qemu: $(BUILD)/dosfs.iso
	qemu-system-i386 -cdrom $(BUILD)/dosfs.iso -m 256M $(QEMU_NET)

qemu-uefi: $(BUILD)/dosfs.iso
	qemu-system-x86_64 -bios /usr/share/edk2/x64/OVMF.4m.fd -cdrom $(BUILD)/dosfs.iso -m 256M $(QEMU_NET)

qemu-usb: $(BUILD)/kernel.elf
	qemu-system-i386 -kernel $(BUILD)/kernel.elf -m 256M $(QEMU_NET)

debug: $(BUILD)/dosfs.iso
	qemu-system-i386 -cdrom $(BUILD)/dosfs.iso -m 256M $(QEMU_NET) -s -S &

clean:
	rm -rf $(BUILD)
	cd rust && cargo clean 2>/dev/null || true

clean-c:
	rm -rf $(BUILD)

.PHONY: all kernel qemu qemu-uefi qemu-usb debug clean clean-c rust-lib mkbuild
