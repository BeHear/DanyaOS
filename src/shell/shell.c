#include "shell.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../drivers/cpuinfo.h"
#include "../drivers/ata.h"
#include "../drivers/acpi.h"
#include "../drivers/rtc.h"
#include "../drivers/pci.h"
#include "../include/io.h"
#include "../memory/pmm.h"
#include "../memory/heap.h"
#include "../process/scheduler.h"
#include "../process/ipc.h"
#include "../fs/tmpfs.h"
#include "../fs/fat16.h"
#include "../libc/string.h"
#include "../tui/tui.h"
#include "../tools/acpi_sim.h"
#include "../tools/cpu_sim.h"
#include "../tools/editor.h"
#include "../net/net.h"

#define CMD_BUF_SIZE 256
#define HISTORY_SIZE 20

static char cmd_buf[CMD_BUF_SIZE];
static int cmd_len = 0;
static char history[HISTORY_SIZE][CMD_BUF_SIZE];
static int history_count = 0;
static int history_pos = 0;

static uint8_t shell_fg = VGA_LIGHT_GREY;
static uint8_t shell_bg = VGA_BLACK;

static void print_prompt(void) {
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("danya");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("@os");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("> ");
    vga_set_color(shell_fg, shell_bg);
}

static void cmd_help(void) {
    vga_clear();
    vga_puts("DanyaOS Shell v1.4.2 - Commands:\n\n");
    vga_puts(" help        clear/cls   echo        uname\n");
    vga_puts(" mem/free    uptime      ps          create\n");
    vga_puts(" ipc         ls          touch       write\n");
    vga_puts(" cat         rm          cp          mv\n");
    vga_puts(" hexdump     color       date        whoami\n");
    vga_puts(" pwd         calc        history     reset\n");
    vga_puts(" beep        about       tuitest     shutdown\n");
    vga_puts(" reboot      cpuinfo     disk        fatls\n");
    vga_puts(" fatread     fatwrite    sacpi       ver\n");
    vga_puts(" sysinfo     pci         colors      random\n");
    vga_puts(" ping        curl\n\n");
    vga_puts(" CPU Simulator:\n");
    vga_puts("  reg [name] [val]  - show/set registers\n");
    vga_puts("  asm <instruction> - execute x86 instruction\n");
    vga_puts("  dump             - dump all registers\n");
    vga_puts("  mem [addr]       - dump simulated memory\n");
    vga_puts("  reset            - reset CPU state\n\n");
    vga_puts(" Code Editor:\n");
    vga_puts("  dano [filename]  - open Dano editor\n");
}

static void cmd_clear(void) {
    vga_clear();
}

static void cmd_echo(const char* args) {
    vga_puts(args);
    vga_putchar('\n');
}

static void cmd_uname(void) {
    vga_puts("DanyaOS 1.4 (Microkernel)\n");
    vga_puts("Architecture: i386\n");
    vga_puts("Build: GCC freestanding + NASM + Rust\n");
}

static void cmd_mem(void) {
    uint32_t free_mem = pmm_get_free_count() * PAGE_SIZE;
    uint32_t total_mem = pmm_get_total_count() * PAGE_SIZE;
    uint32_t used_mem = total_mem - free_mem;

    vga_printf("Total:  %u KB\n", total_mem / 1024);
    vga_printf("Used:   %u KB\n", used_mem / 1024);
    vga_printf("Free:   %u KB\n", free_mem / 1024);
}

static void cmd_uptime(void) {
    uint32_t ticks = timer_get_ticks();
    uint32_t seconds = ticks / 100;
    vga_printf("Ticks: %u  (%u sec)\n", ticks, seconds);
}

static void cmd_ps(void) {
    vga_puts("  PID  NAME               STATE\n");
    vga_puts("  ---  ----               -----\n");
    int found = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* p = scheduler_get(i + 1);
        if (p) {
            found = 1;
            const char* state = "???";
            switch (p->state) {
                case PROC_READY:   state = "READY"; break;
                case PROC_RUNNING: state = "RUNNING"; break;
                case PROC_BLOCKED: state = "BLOCKED"; break;
                case PROC_ZOMBIE:  state = "ZOMBIE"; break;
                default: break;
            }
            vga_printf("  %-4d %-18s %s\n", p->pid, p->name, state);
        }
    }
    if (!found) vga_puts("  (no processes)\n");
}

static void cmd_touch(const char* name) {
    while (*name == ' ') name++;
    if (*name == '\0') { vga_puts("Usage: touch <filename>\n"); return; }
    if (tmpfs_create(name) == 0)
        vga_printf("Created: %s\n", name);
    else
        vga_puts("Error creating file\n");
}

static void cmd_write_file(const char* args) {
    while (*args == ' ') args++;
    char name[64];
    int i = 0;
    while (*args && *args != ' ' && i < 63) name[i++] = *args++;
    name[i] = '\0';
    while (*args == ' ') args++;
    if (i == 0 || *args == '\0') {
        vga_puts("Usage: write <filename> <data>\n");
        return;
    }
    tmpfs_write(name, args, strlen(args));
    vga_printf("Wrote %d bytes to %s\n", strlen(args), name);
}

static void cmd_cat(const char* name) {
    while (*name == ' ') name++;
    if (*name == '\0') { vga_puts("Usage: cat <filename>\n"); return; }
    char* buf = (char*)kmalloc(TMPFS_DATA_SIZE + 1);
    if (!buf) { vga_puts("Out of memory\n"); return; }
    int len = tmpfs_read(name, buf, TMPFS_DATA_SIZE);
    if (len < 0) { kfree(buf); vga_printf("File not found: %s\n", name); return; }
    buf[len] = '\0';
    vga_puts(buf);
    vga_putchar('\n');
    kfree(buf);
}

static void cmd_rm(const char* name) {
    while (*name == ' ') name++;
    if (*name == '\0') { vga_puts("Usage: rm <filename>\n"); return; }
    if (tmpfs_delete(name) == 0)
        vga_printf("Deleted: %s\n", name);
    else
        vga_printf("Not found: %s\n", name);
}

static void cmd_cp(const char* args) {
    while (*args == ' ') args++;
    char src[64], dst[64];
    int i = 0;
    while (*args && *args != ' ' && i < 63) src[i++] = *args++;
    src[i] = '\0';
    while (*args == ' ') args++;
    if (i == 0 || *args == '\0') { vga_puts("Usage: cp <src> <dst>\n"); return; }
    i = 0;
    while (*args && *args != ' ' && i < 63) dst[i++] = *args++;
    dst[i] = '\0';
    char* buf = (char*)kmalloc(TMPFS_DATA_SIZE);
    if (!buf) { vga_puts("Out of memory\n"); return; }
    int len = tmpfs_read(src, buf, TMPFS_DATA_SIZE);
    if (len < 0) { kfree(buf); vga_printf("Source not found: %s\n", src); return; }
    tmpfs_write(dst, buf, len);
    kfree(buf);
    vga_printf("Copied %s -> %s (%d bytes)\n", src, dst, len);
}

static void cmd_mv(const char* args) {
    while (*args == ' ') args++;
    char src[64], dst[64];
    int i = 0;
    while (*args && *args != ' ' && i < 63) src[i++] = *args++;
    src[i] = '\0';
    while (*args == ' ') args++;
    if (i == 0 || *args == '\0') { vga_puts("Usage: mv <src> <dst>\n"); return; }
    i = 0;
    while (*args && *args != ' ' && i < 63) dst[i++] = *args++;
    dst[i] = '\0';
    char* buf = (char*)kmalloc(TMPFS_DATA_SIZE);
    if (!buf) { vga_puts("Out of memory\n"); return; }
    int len = tmpfs_read(src, buf, TMPFS_DATA_SIZE);
    if (len < 0) { kfree(buf); vga_printf("Source not found: %s\n", src); return; }
    tmpfs_write(dst, buf, len);
    kfree(buf);
    tmpfs_delete(src);
    vga_printf("Moved %s -> %s\n", src, dst);
}

static void cmd_hexdump(const char* args) {
    while (*args == ' ') args++;
    if (*args == '\0') { vga_puts("Usage: hexdump [mem <addr> <len> | <filename>]\n"); return; }

    // Memory dump mode: hexdump mem <hex_addr> <len>
    if (strncmp(args, "mem ", 4) == 0) {
        args += 4;
        while (*args == ' ') args++;
        uint32_t addr = 0;
        int digits = 0;
        while ((*args >= '0' && *args <= '9') ||
               (*args >= 'a' && *args <= 'f') ||
               (*args >= 'A' && *args <= 'F')) {
            addr <<= 4;
            if (*args >= '0' && *args <= '9') addr += *args - '0';
            else if (*args >= 'a' && *args <= 'f') addr += *args - 'a' + 10;
            else addr += *args - 'A' + 10;
            args++; digits++;
        }
        if (digits == 0) { vga_puts("Invalid address\n"); return; }
        while (*args == ' ') args++;
        int mlen = atoi(args);
        if (mlen <= 0) mlen = 256;
        if (mlen > 4096) mlen = 4096; // safety limit

        vga_printf("Memory dump at 0x%x (%d bytes):\n", addr, mlen);
        uint8_t* ptr = (uint8_t*)addr;
        for (int i = 0; i < mlen; i += 16) {
            vga_printf("%08x: ", addr + i);
            for (int j = 0; j < 16 && (i + j) < mlen; j++) {
                vga_printf("%02x ", ptr[i + j]);
            }
            vga_puts(" |");
            for (int j = 0; j < 16 && (i + j) < mlen; j++) {
                uint8_t ch = ptr[i + j];
                vga_putchar((ch >= 32 && ch < 127) ? (char)ch : '.');
            }
            vga_puts("|\n");
        }
        return;
    }

    // File hexdump mode with ASCII column
    char* buf = (char*)kmalloc(TMPFS_DATA_SIZE);
    if (!buf) { vga_puts("Out of memory\n"); return; }
    int len = tmpfs_read(args, buf, TMPFS_DATA_SIZE);
    if (len < 1) { kfree(buf); vga_printf("File not found or empty: %s\n", args); return; }
    vga_printf("Hexdump of %s (%d bytes):\n", args, len);
    for (int i = 0; i < len; i += 16) {
        vga_printf("%08x: ", i);
        for (int j = 0; j < 16 && (i + j) < len; j++) {
            vga_printf("%02x ", (uint8_t)buf[i + j]);
        }
        // Padding if last line is short
        int remaining = len - i;
        if (remaining < 16) {
            for (int p = remaining; p < 16; p++) vga_puts("   ");
        }
        vga_puts(" |");
        for (int j = 0; j < 16 && (i + j) < len; j++) {
            uint8_t ch = (uint8_t)buf[i + j];
            vga_putchar((ch >= 32 && ch < 127) ? (char)ch : '.');
        }
        vga_puts("|\n");
    }
    kfree(buf);
}

static void cmd_color(const char* args) {
    static const char* color_names[] = {
        "black", "blue", "green", "cyan", "red", "magenta",
        "brown", "light_grey", "dark_grey", "light_blue",
        "light_green", "light_cyan", "light_red", "light_magenta",
        "yellow", "white"
    };

    while (*args == ' ') args++;

    if (*args == '\0') {
        vga_printf("Current: fg=%s bg=%s\n",
                   color_names[shell_fg], color_names[shell_bg]);
        vga_puts("Usage: color <fg> [<bg>]\n");
        vga_puts("       color reset\n");
        return;
    }

    if (strncmp(args, "reset", 5) == 0 && (args[5] == '\0' || args[5] == ' ')) {
        shell_fg = VGA_LIGHT_GREY;
        shell_bg = VGA_BLACK;
        vga_set_color(shell_fg, shell_bg);
        vga_puts("Color reset to default.\n");
        return;
    }

    int fg = -1;
    for (int i = 0; i < 16; i++) {
        size_t len = strlen(color_names[i]);
        if (strncmp(args, color_names[i], len) == 0 &&
            (args[len] == '\0' || args[len] == ' ')) {
            fg = i;
            args += len;
            break;
        }
    }

    if (fg < 0) {
        vga_puts("Unknown color.\n");
        return;
    }

    while (*args == ' ') args++;

    if (*args == '\0') {
        shell_fg = (uint8_t)fg;
        vga_set_color(shell_fg, shell_bg);
        vga_printf("Foreground: %s\n", color_names[shell_fg]);
        return;
    }

    int bg = -1;
    for (int i = 0; i < 16; i++) {
        size_t len = strlen(color_names[i]);
        if (strncmp(args, color_names[i], len) == 0 &&
            (args[len] == '\0' || args[len] == ' ')) {
            bg = i;
            break;
        }
    }

    if (bg < 0) {
        vga_puts("Unknown background color.\n");
        return;
    }

    shell_fg = (uint8_t)fg;
    shell_bg = (uint8_t)bg;
    vga_set_color(shell_fg, shell_bg);
    vga_printf("Color: %s on %s\n", color_names[shell_fg], color_names[shell_bg]);
}

static void cmd_date(void) {
    rtc_time_t rtc;
    if (rtc_read_time(&rtc) == 0) {
        char buf[64];
        rtc_format_time(buf, sizeof(buf), &rtc);
        vga_printf("%s  (Weekday: %u)\n", buf, rtc.weekday);
    } else {
        vga_puts("RTC not available. Use 'uptime' for boot timer.\n");
    }
}

static void cmd_ver(void) {
    vga_puts("DanyaOS v1.4 (2025-06-26)\n");
    vga_puts("  Kernel: Microkernel with IPC\n");
    vga_puts("  Drivers: ATA/IDE, FAT16, ACPI, RTC, PCI\n");
    vga_puts("  Features: Paging, VMM, Scheduler, tmpfs\n");
}

static void cmd_sysinfo(void) {
    uint32_t total_mem = pmm_get_total_count() * PAGE_SIZE;
    uint32_t free_mem = pmm_get_free_count() * PAGE_SIZE;
    uint32_t used_mem = total_mem - free_mem;
    uint32_t ticks = timer_get_ticks();
    uint32_t seconds = ticks / 100;

    vga_puts("===== DanyaOS System Information =====\n");
    vga_printf("Version:     DanyaOS 1.4\n");
    vga_printf("Arch:        i386\n");
    vga_printf("Memory:      %u KB total, %u KB used, %u KB free\n",
               total_mem / 1024, used_mem / 1024, free_mem / 1024);
    vga_printf("Uptime:      %u seconds\n", seconds);
    vga_printf("Processes:   %d registered\n", scheduler_process_count());
    vga_printf("PCI devices: %d found\n", pci_device_count());

    rtc_time_t rtc;
    if (rtc_read_time(&rtc) == 0) {
        char buf[64];
        rtc_format_time(buf, sizeof(buf), &rtc);
        vga_printf("RTC time:    %s\n", buf);
    }
}

static void cmd_pci(void) {
    int count = pci_device_count();
    vga_printf("PCI devices: %d\n\n", count);
    vga_puts("BUS:DEV.F  VENDOR  DEVICE  CLASS     IRQ  NAME\n");
    vga_puts("--------  ------  ------  --------  ---  ----\n");
    for (int i = 0; i < count; i++) {
        const pci_device_t* d = pci_get_device(i);
        if (!d) continue;
        vga_printf("%02x:%02x.%x  %04x   %04x  %s  %02x  %s\n",
                   d->bus, d->device, d->func,
                   d->vendor_id, d->device_id,
                   pci_class_name(d->class_code, d->subclass),
                   d->interrupt_line,
                   pci_vendor_name(d->vendor_id));
    }
}

static void cmd_colors(void) {
    vga_puts("VGA Color Palette (foreground on background):\n\n");
    for (int bg = 0; bg < 16; bg++) {
        for (int fg = 0; fg < 16; fg++) {
            vga_set_color((uint8_t)fg, (uint8_t)bg);
            vga_putchar('#');
        }
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        vga_putchar('\n');
    }
    vga_puts("\nUse: color <fg> [<bg>] — e.g. 'color red white'\n");
    vga_puts("     color reset — restore defaults\n");
    vga_set_color(shell_fg, shell_bg);
}

// Simple LFSR PRNG — no external state needed
static uint32_t rand_state = 1;
static void cmd_random(const char* args) {
    while (*args == ' ') args++;
    int limit = atoi(args);
    if (limit <= 0) limit = 100;

    // LFSR
    for (int i = 0; i < 5; i++) {
        rand_state = (rand_state >> 1) ^ (-(rand_state & 1) & 0xEDB88320);
    }
    int val = (int)(rand_state % (uint32_t)limit);
    vga_printf("Random (0-%d): %d\n", limit - 1, val);
}

static void cmd_whoami(void) {
    vga_puts("root\n");
}

static void cmd_pwd(void) {
    vga_puts("/\n");
}

static void cmd_calc(const char* expr) {
    while (*expr == ' ') expr++;
    if (*expr == '\0') { vga_puts("Usage: calc <number> <op> <number>\n"); return; }
    int a = atoi(expr);
    while (*expr && *expr != ' ') expr++;
    while (*expr == ' ') expr++;
    char op = *expr++;
    while (*expr == ' ') expr++;
    int b = atoi(expr);
    int res = 0;
    switch (op) {
        case '+': res = a + b; break;
        case '-': res = a - b; break;
        case '*': res = a * b; break;
        case '/':
            if (b == 0) { vga_puts("Error: division by zero\n"); return; }
            res = a / b;
            break;
        case '%':
            if (b == 0) { vga_puts("Error: division by zero\n"); return; }
            res = a % b;
            break;
        default: vga_puts("Unknown operator. Use: + - * / %%\n"); return;
    }
    vga_printf("%d %c %d = %d\n", a, op, b, res);
}

static void cmd_history(void) {
    vga_printf("Command history (%d):\n", history_count);
    for (int i = 0; i < history_count; i++) {
        vga_printf("  %d: %s\n", i + 1, history[i]);
    }
}

static void cmd_reset(void) {
    vga_clear();
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("Terminal reset.\n");
}

static void cmd_beep(void) {
    outb(0x61, inb(0x61) | 0x03);
    for (volatile int i = 0; i < 100000; i++);
    outb(0x61, inb(0x61) & ~0x03);
    vga_puts("Beep!\n");
}

static void cmd_about(void) {
    vga_puts("DanyaOS v1.4\n");
    vga_puts("A hobby microkernel OS for x86 (i386)\n");
    vga_puts("Written in C, Rust, and x86 assembly\n");
    vga_puts("Features: GDT, IDT, PMM, VMM, Heap,\n");
    vga_puts("  Scheduler, IPC, Syscalls, tmpfs,\n");
    vga_puts("  FAT16, ATA/IDE, ACPI, CPU Sim, Dano\n");
    vga_puts("(c) 2025 DanyaOS Project\n");
}

static void cmd_cpuinfo(void) {
    cpu_info_t info;
    cpuinfo_detect(&info);

    vga_printf("Vendor:   %s\n", info.vendor);
    if (info.brand[0])
        vga_printf("Brand:    %s\n", info.brand);

    uint32_t disp_family = info.family + info.ext_family;
    uint32_t disp_model  = info.model;
    if (info.family == 6 || info.family == 15)
        disp_model = info.model + (info.ext_model << 4);
    if (info.family == 15)
        disp_family = info.family + info.ext_family;

    vga_printf("Family:   %u  Model: %u  Stepping: %u\n",
               disp_family, disp_model, info.stepping);

    vga_puts("Features: ");
    if (info.features_edx & (1 << 4))  vga_puts("TSC ");
    if (info.features_edx & (1 << 5))  vga_puts("MSR ");
    if (info.features_edx & (1 << 8))  vga_puts("CX8 ");
    if (info.features_edx & (1 << 15)) vga_puts("CMOV ");
    if (info.features_edx & (1 << 23)) vga_puts("MMX ");
    if (info.features_edx & (1 << 25)) vga_puts("SSE ");
    if (info.features_edx & (1 << 26)) vga_puts("SSE2 ");
    if (info.features_ecx & (1 << 0))  vga_puts("SSE3 ");
    if (info.features_ecx & (1 << 9))  vga_puts("SSSE3 ");
    if (info.features_ecx & (1 << 19)) vga_puts("SSE4.1 ");
    if (info.features_ecx & (1 << 20)) vga_puts("SSE4.2 ");
    if (info.features_edx & (1 << 29)) vga_puts("TM ");
    vga_putchar('\n');
}

static void cmd_shutdown(void) {
    vga_puts("Shutting down...\n");
    acpi_shutdown();
    cli();
    hlt();
}

static void cmd_disk(void) {
    ata_device_t* dev = ata_get_device();
    if (!dev) {
        vga_puts("No ATA devices found\n");
        return;
    }
    vga_printf("Drive: %s\n", dev->model);
    vga_printf("Size:  %u MB (%u sectors)\n",
               (dev->max_lba * 512) / (1024 * 1024),
               dev->max_lba);
}

static void cmd_fatls(void) {
    fat16_list_files();
}

static void cmd_fatread(const char* name) {
    while (*name == ' ') name++;
    if (*name == '\0') { vga_puts("Usage: fatread <filename>\n"); return; }

    char* buf = (char*)kmalloc(4096);
    if (!buf) { vga_puts("Out of memory\n"); return; }
    int len = fat16_read_file(name, buf, 4095);
    if (len < 0) { kfree(buf); vga_printf("File not found: %s\n", name); return; }
    buf[len] = '\0';
    vga_puts(buf);
    vga_putchar('\n');
    kfree(buf);
}

static void cmd_fatwrite(const char* args) {
    while (*args == ' ') args++;
    char name[64];
    int i = 0;
    while (*args && *args != ' ' && i < 63) name[i++] = *args++;
    name[i] = '\0';
    while (*args == ' ') args++;
    if (i == 0 || *args == '\0') {
        vga_puts("Usage: fatwrite <filename> <data>\n");
        return;
    }
    int written = fat16_write_file(name, args, strlen(args));
    if (written >= 0)
        vga_printf("Wrote %d bytes to %s\n", written, name);
    else
        vga_puts("Error writing file\n");
}

static void cmd_create_process(const char* name) {
    while (*name == ' ') name++;
    if (*name == '\0') { vga_puts("Usage: create <name>\n"); return; }
    vga_puts("Process creation requires a valid entry point.\n");
}

static void cmd_ipc_test(void) {
    process_t* proc = scheduler_current();
    if (proc) {
        ipc_send(proc->pid, "Hello from shell!", 17);
        vga_printf("IPC test: message queued (pid=%d).\n", proc->pid);
    } else {
        vga_puts("IPC test: no current process.\n");
    }
}

static void cmd_reboot(void) {
    vga_puts("Rebooting...\n");
    acpi_reboot();
    cli();
    hlt();
}

static void cmd_ping(const char* args) {
    while (*args == ' ') args++;
    if (*args == '\0') { vga_puts("Usage: ping <ip>\n"); return; }
    int a = 0, b = 0, c = 0, d = 0;
    const char* p = args;
    while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
    if (*p == '.') p++;
    while (*p >= '0' && *p <= '9') b = b * 10 + (*p++ - '0');
    if (*p == '.') p++;
    while (*p >= '0' && *p <= '9') c = c * 10 + (*p++ - '0');
    if (*p == '.') p++;
    while (*p >= '0' && *p <= '9') d = d * 10 + (*p++ - '0');
    if (a == 0 && b == 0 && c == 0 && d == 0 && *args != '0') {
        vga_puts("Invalid IP format.\n");
        return;
    }
    uint32_t ip = ((uint32_t)a << 24) | ((uint32_t)b << 16) |
                  ((uint32_t)c << 8) | (uint32_t)d;
    vga_printf("PING %d.%d.%d.%d: 64 bytes of data.\n", a, b, c, d);
    for (int i = 0; i < 4; i++) {
        int ms = net_ping(ip);
        if (ms == -1) {
            vga_puts("  Request failed (network error)\n");
        } else if (ms == -2) {
            vga_printf("  Request timeout (400ms)\n");
        } else {
            vga_printf("  64 bytes: time=%d ms\n", ms * 10);
        }
        for (volatile int j = 0; j < 200000; j++);
    }
}

static void cmd_curl(const char* args) {
    while (*args == ' ') args++;
    if (*args == '\0') { vga_puts("Usage: curl <ip:port/path>\n"); return; }

    char host[32] = {0};
    uint16_t port = 80;
    const char* path = "/";

    // Parse ip:port/path
    const char* p = args;
    int i = 0;
    while (*p && *p != ':' && *p != '/' && i < 31) host[i++] = *p++;
    host[i] = '\0';
    if (*p == ':') {
        p++;
        port = 0;
        while (*p >= '0' && *p <= '9') { port = port * 10 + (*p - '0'); p++; }
    }
    if (*p == '/') path = p;

    vga_printf("Connecting to %s:%d...\n", host, port);
    if (tcp_connect(host, port) != 0) {
        vga_puts("Connection failed.\n");
        return;
    }
    vga_puts("Connected!\n");

    // Send HTTP GET
    char req[256];
    int rlen = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", path, host);
    tcp_send_data((uint8_t*)req, rlen);

    // Receive response
    vga_puts("--- Response ---\n");
    uint8_t buf[4096];
    int total = 0;
    for (int i = 0; i < 100; i++) {
        int n = tcp_recv(buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            vga_puts((char*)buf);
            total += n;
        }
        if (n == 0 && i > 10) break;
    }
    if (total == 0) vga_puts("(no data received)\n");
    else {
        vga_printf("\n--- %d bytes received ---\n", total);
    }
    tcp_close();
}

static void add_to_history(const char* cmd) {
    if (history_count < HISTORY_SIZE) {
        strncpy(history[history_count], cmd, CMD_BUF_SIZE - 1);
        history[history_count][CMD_BUF_SIZE - 1] = '\0';
        history_count++;
    } else {
        for (int i = 0; i < HISTORY_SIZE - 1; i++) {
            strncpy(history[i], history[i + 1], CMD_BUF_SIZE - 1);
            history[i][CMD_BUF_SIZE - 1] = '\0';
        }
        strncpy(history[HISTORY_SIZE - 1], cmd, CMD_BUF_SIZE - 1);
        history[HISTORY_SIZE - 1][CMD_BUF_SIZE - 1] = '\0';
    }
}

static void process_command(const char* cmd) {
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') return;

    add_to_history(cmd);

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) cmd_help();
    else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) cmd_clear();
    else if (strncmp(cmd, "echo", 4) == 0 && (cmd[4] == ' ' || cmd[4] == '\0')) cmd_echo(cmd + 4);
    else if (strcmp(cmd, "uname") == 0) cmd_uname();
    else if (strcmp(cmd, "mem") == 0 || strcmp(cmd, "free") == 0) cmd_mem();
    else if (strcmp(cmd, "uptime") == 0) cmd_uptime();
    else if (strcmp(cmd, "ps") == 0) cmd_ps();
    else if (strncmp(cmd, "touch", 5) == 0 && (cmd[5] == ' ' || cmd[5] == '\0')) cmd_touch(cmd + 5);
    else if (strncmp(cmd, "write", 5) == 0 && (cmd[5] == ' ' || cmd[5] == '\0')) cmd_write_file(cmd + 5);
    else if (strncmp(cmd, "cat", 3) == 0 && (cmd[3] == ' ' || cmd[3] == '\0')) cmd_cat(cmd + 3);
    else if (strncmp(cmd, "rm", 2) == 0 && (cmd[2] == ' ' || cmd[2] == '\0')) cmd_rm(cmd + 2);
    else if (strncmp(cmd, "cp", 2) == 0 && (cmd[2] == ' ' || cmd[2] == '\0')) cmd_cp(cmd + 2);
    else if (strncmp(cmd, "mv", 2) == 0 && (cmd[2] == ' ' || cmd[2] == '\0')) cmd_mv(cmd + 2);
    else if (strncmp(cmd, "hexdump", 7) == 0 && (cmd[7] == ' ' || cmd[7] == '\0')) cmd_hexdump(cmd + 7);
    else if (strcmp(cmd, "ls") == 0) tmpfs_list();
    else if (strcmp(cmd, "reboot") == 0) cmd_reboot();
    else if (strcmp(cmd, "shutdown") == 0) cmd_shutdown();
    else if (strncmp(cmd, "color", 5) == 0 && (cmd[5] == ' ' || cmd[5] == '\0')) cmd_color(cmd + 5);
    else if (strncmp(cmd, "create", 6) == 0 && (cmd[6] == ' ' || cmd[6] == '\0')) cmd_create_process(cmd + 6);
    else if (strcmp(cmd, "ipc") == 0) cmd_ipc_test();
    else if (strcmp(cmd, "date") == 0) cmd_date();
    else if (strcmp(cmd, "whoami") == 0) cmd_whoami();
    else if (strcmp(cmd, "pwd") == 0) cmd_pwd();
    else if (strncmp(cmd, "calc", 4) == 0 && (cmd[4] == ' ' || cmd[4] == '\0')) cmd_calc(cmd + 4);
    else if (strcmp(cmd, "history") == 0) cmd_history();
    else if (strcmp(cmd, "reset") == 0) cmd_reset();
    else if (strcmp(cmd, "beep") == 0) cmd_beep();
    else if (strcmp(cmd, "about") == 0) cmd_about();
    else if (strcmp(cmd, "tuitest") == 0) tui_test();
    else if (strcmp(cmd, "cpuinfo") == 0) cmd_cpuinfo();
    else if (strcmp(cmd, "ver") == 0) cmd_ver();
    else if (strcmp(cmd, "sysinfo") == 0) cmd_sysinfo();
    else if (strcmp(cmd, "pci") == 0) cmd_pci();
    else if (strcmp(cmd, "colors") == 0) cmd_colors();
    else if (strncmp(cmd, "random", 6) == 0 && (cmd[6] == ' ' || cmd[6] == '\0')) cmd_random(cmd + 6);
    else if (strcmp(cmd, "disk") == 0) cmd_disk();
    else if (strcmp(cmd, "fatls") == 0) cmd_fatls();
    else if (strncmp(cmd, "fatread", 7) == 0 && (cmd[7] == ' ' || cmd[7] == '\0')) cmd_fatread(cmd + 7);
    else if (strncmp(cmd, "fatwrite", 8) == 0 && (cmd[8] == ' ' || cmd[8] == '\0')) cmd_fatwrite(cmd + 8);
    else if (strncmp(cmd, "ping", 4) == 0 && (cmd[4] == ' ' || cmd[4] == '\0')) cmd_ping(cmd + 4);
    else if (strncmp(cmd, "curl", 4) == 0 && (cmd[4] == ' ' || cmd[4] == '\0')) cmd_curl(cmd + 4);
    else if (strncmp(cmd, "sacpi", 5) == 0 && (cmd[5] == ' ' || cmd[5] == '\0')) acpi_sim_execute_command(cmd + 5);
    else if (strcmp(cmd, "dump") == 0) cpu_sim_dump();
    else if (strncmp(cmd, "asm", 3) == 0 && (cmd[3] == ' ' || cmd[3] == '\0')) cpu_sim_execute(cmd + 3);
    else if (strncmp(cmd, "reg", 3) == 0 && (cmd[3] == ' ' || cmd[3] == '\0')) {
        const char* args = cmd + 3;
        while (*args == ' ') args++;
        if (*args == '\0') { cpu_sim_dump(); }
        else {
            char name[16];
            int i = 0;
            while (*args && *args != ' ' && i < 15) name[i++] = *args++;
            name[i] = '\0';
            while (*args == ' ') args++;
            if (*args) {
                uint32_t val = 0;
                if (args[0] == '0' && args[1] == 'x') {
                    args += 2;
                    while (*args) {
                        val <<= 4;
                        if (*args >= '0' && *args <= '9') val += *args - '0';
                        else if (*args >= 'a' && *args <= 'f') val += *args - 'a' + 10;
                        else if (*args >= 'A' && *args <= 'F') val += *args - 'A' + 10;
                        args++;
                    }
                } else {
                    while (*args >= '0' && *args <= '9') { val = val * 10 + (*args - '0'); args++; }
                }
                cpu_sim_set_reg(name, val);
                vga_printf("  %s = 0x%x\n", name, val);
            } else {
                vga_printf("  %s = 0x%x\n", name, cpu_sim_get_reg(name));
            }
        }
    }
    else if (strncmp(cmd, "dano", 4) == 0 && (cmd[4] == ' ' || cmd[4] == '\0')) {
        const char* fn = cmd + 4;
        while (*fn == ' ') fn++;
        if (*fn) editor_open(fn);
        else editor_new();
        editor_run();
    }
    else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("Unknown command: %s\n", cmd);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

void shell_init(void) {
    cmd_len = 0;
    memset(cmd_buf, 0, CMD_BUF_SIZE);
    memset(history, 0, sizeof(history));
    history_count = 0;
    history_pos = 0;
    shell_fg = VGA_LIGHT_GREY;
    shell_bg = VGA_BLACK;
}

void shell_run(void) {
    print_prompt();

    while (1) {
        char c = keyboard_getchar();

        if (c == '\n') {
            vga_putchar('\n');
            cmd_buf[cmd_len] = '\0';
            if (cmd_len > 0) {
                process_command(cmd_buf);
            }
            cmd_len = 0;
            memset(cmd_buf, 0, CMD_BUF_SIZE);
            print_prompt();
        } else if (c == '\b') {
            if (cmd_len > 0) {
                cmd_len--;
                cmd_buf[cmd_len] = '\0';
                vga_putchar('\b');
            }
        } else if (cmd_len < CMD_BUF_SIZE - 1) {
            cmd_buf[cmd_len++] = c;
            vga_putchar(c);
        }
    }
}
