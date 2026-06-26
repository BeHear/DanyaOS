#include "kernel.h"
#include "gdt.h"
#include "idt.h"
#include "../drivers/vga.h"
#include "../drivers/serial.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../drivers/ata.h"
#include "../drivers/acpi.h"
#include "../drivers/rtc.h"
#include "../drivers/pci.h"
#include "../memory/pmm.h"
#include "../memory/vmm.h"
#include "../memory/heap.h"
#include "../process/scheduler.h"
#include "../process/ipc.h"
#include "../syscall/syscall.h"
#include "../fs/tmpfs.h"
#include "../fs/fat16.h"
#include "../shell/shell.h"
#include "../tools/acpi_sim.h"
#include "../include/io.h"
#include "../include/multiboot.h"
#include "../libc/string.h"
#include "../drivers/ne2000.h"
#include "../net/net.h"

static void print_banner(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n");
    vga_puts("  _____   _   _  _____  ____    ____  _   _ \n");
    vga_puts(" |  _  \\ | | | ||  ___||  _ \\  / ___|| | | |\n");
    vga_puts(" | | | | | |_| || |_   | | | || |    | |_| |\n");
    vga_puts(" | | | | |  _  ||  _|  | | | || |    |  _  |\n");
    vga_puts(" | |_| | | | | || |    | |_| || |___ | | | |\n");
    vga_puts(" |_____/ |_| |_||_|    |____/  \\____||_| |_|\n");
    vga_puts("\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("  Microkernel v1.4.2\n");
    vga_puts("  (c) 2025 DanyaOS Project\n\n");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("  Initializing subsystems...\n");
}

static uint32_t detect_memory_multiboot(multiboot_info_t* mbi) {
    if (!(mbi->flags & MULTIBOOT_FLAG_MEM)) return 16 * 1024 * 1024;

    uint64_t upper_mem_kb = mbi->mem_upper;
    uint64_t total = (upper_mem_kb + 1024) * 1024;
    if (total > 0xFFFFFFFF) total = 0xFFFFFFFF;

    // Cap to identity-mapped region (16MB). PMM must not hand out pages
    // outside the range the VMM identity-maps, otherwise page-table setup
    // would fault on unmapped physical addresses after paging is enabled.
    uint64_t cap = 16 * 1024 * 1024;
    if (total > cap) total = cap;

    vga_printf("  [ OK ] Multiboot: %u KB lower, %u KB upper\n",
               mbi->mem_lower, mbi->mem_upper);
    return total;
}

static void init_subsystems(multiboot_info_t* mbi) {
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    serial_puts("[init] GDT...");
    gdt_init();
    vga_puts("  [ OK ] GDT\n");
    serial_puts(" done\n");

    serial_puts("[init] IDT...");
    idt_init();
    vga_puts("  [ OK ] IDT\n");
    serial_puts(" done\n");

    uint32_t total_mem = detect_memory_multiboot(mbi);

    serial_puts("[init] PMM...");
    pmm_init(total_mem);
    vga_printf("  [ OK ] PMM (%u MB)\n", total_mem / (1024 * 1024));
    serial_puts(" done\n");

    serial_puts("[init] VMM...");
    vmm_init();
    vga_puts("  [ OK ] VMM\n");
    serial_puts(" done\n");

    serial_puts("[init] Heap...");
    heap_init();
    vga_puts("  [ OK ] Heap\n");
    serial_puts(" done\n");

    serial_puts("[init] Timer...");
    timer_init(100);
    vga_puts("  [ OK ] Timer (100Hz)\n");
    serial_puts(" done\n");

    serial_puts("[init] Keyboard...");
    keyboard_init();
    vga_puts("  [ OK ] Keyboard\n");
    serial_puts(" done\n");

    serial_puts("[init] Scheduler...");
    scheduler_init();
    vga_puts("  [ OK ] Scheduler\n");
    serial_puts(" done\n");

    serial_puts("[init] IPC...");
    ipc_init();
    vga_puts("  [ OK ] IPC\n");
    serial_puts(" done\n");

    serial_puts("[init] Syscalls...");
    syscall_init();
    vga_puts("  [ OK ] Syscalls\n");
    serial_puts(" done\n");

    serial_puts("[init] ATA...");
    ata_init();
    serial_puts(" done\n");

    serial_puts("[init] RTC...");
    rtc_init();
    serial_puts(" done\n");

    serial_puts("[init] PCI...");
    pci_init();
    serial_puts(" done\n");

    serial_puts("[init] ACPI...");
    acpi_init();
    serial_puts(" done\n");

    serial_puts("[init] ACPI sim...");
    acpi_sim_init();
    serial_puts(" done\n");

    serial_puts("[init] tmpfs...");
    tmpfs_init();
    vga_puts("  [ OK ] tmpfs\n");
    serial_puts(" done\n");

    serial_puts("[init] FAT16...");
    if (fat16_mount() != 0) {
        vga_puts("  [SKIP] FAT16: No FAT16 filesystem found\n");
    }
    serial_puts(" done\n");

    serial_puts("[init] NE2000...");
    ne2000_init();
    serial_puts(" done\n");

    serial_puts("[init] Network stack...");
    net_init();
    serial_puts(" done\n");

    sti();
    serial_puts("[init] All subsystems initialized!\n");
}

void kernel_main(uint32_t magic, multiboot_info_t* mbi) {
    serial_init();

    if (magic != MULTIBOOT_BOOT_MAGIC && magic != MULTIBOOT2_MAGIC) {
        serial_puts("[kernel] ERROR: not booted by Multiboot loader!\n");
        vga_puts("\n  ERROR: Not booted by GRUB/Multiboot!\n");
        vga_puts("  This OS requires GRUB to boot.\n");
        while (1) hlt();
    }

    if (!mbi) {
        serial_puts("[kernel] ERROR: multiboot info is NULL!\n");
        vga_puts("\n  ERROR: Invalid multiboot info pointer!\n");
        while (1) hlt();
    }

    serial_puts("[kernel] Multiboot verified OK\n");

    vga_init();
    serial_puts("[kernel] vga_init done\n");

    print_banner();
    serial_puts("[kernel] banner printed\n");

    init_subsystems(mbi);
    serial_puts("[kernel] subsystems initialized\n");

    /* Auto-test: ping 10.0.2.2 */
    serial_puts("[nettest] PING 10.0.2.2 ... ");
    { int r = net_ping(0x0A000202);
      serial_puts(r >= 0 ? "OK\n" : "FAIL\n"); }

    /* Auto-test: ping 8.8.8.8 */
    serial_puts("[nettest] PING 8.8.8.8 ... ");
    { int r = net_ping(0x08080808);
      serial_puts(r >= 0 ? "OK\n" : "FAIL\n"); }

    serial_puts("[nettest] icmp_recv="); serial_puts_hex(icmp_recv_count);
    serial_puts(" last_type="); serial_puts_hex(last_icmp_type);
    serial_puts("\n");
    serial_puts("[nettest] done\n");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n  All subsystems initialized successfully!\n\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("  Type 'help' for available commands.\n\n");
    serial_puts("[kernel] entering shell\n");

    shell_init();
    shell_run();

    while (1) {
        hlt();
    }
}
