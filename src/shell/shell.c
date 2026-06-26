#include "shell.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../drivers/cpuinfo.h"
#include "../drivers/ata.h"
#include "../drivers/acpi.h"
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
    vga_puts("DanyaOS Shell v1.3.5 - Commands:\n\n");
    vga_puts(" help        clear/cls   echo        uname\n");
    vga_puts(" mem/free    uptime      ps          create\n");
    vga_puts(" ipc         ls          touch       write\n");
    vga_puts(" cat         rm          cp          mv\n");
    vga_puts(" hexdump     color       date        whoami\n");
    vga_puts(" pwd         calc        history     reset\n");
    vga_puts(" beep        about       tuitest     shutdown\n");
    vga_puts(" reboot      cpuinfo     disk        fatls\n");
    vga_puts(" fatread     fatwrite    sacpi\n\n");
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
    vga_puts("DanyaOS 1.3.5 (Microkernel)\n");
    vga_puts("Architecture: i386\n");
    vga_puts("Build: GCC freestanding\n");
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

static void cmd_hexdump(const char* name) {
    while (*name == ' ') name++;
    if (*name == '\0') { vga_puts("Usage: hexdump <filename>\n"); return; }
    char* buf = (char*)kmalloc(TMPFS_DATA_SIZE);
    if (!buf) { vga_puts("Out of memory\n"); return; }
    int len = tmpfs_read(name, buf, TMPFS_DATA_SIZE);
    if (len < 1) { kfree(buf); vga_printf("File not found or empty: %s\n", name); return; }
    vga_printf("Hexdump of %s (%d bytes):\n", name, len);
    for (int i = 0; i < len; i += 16) {
        vga_printf("%04x: ", i);
        for (int j = 0; j < 16 && (i + j) < len; j++) {
            vga_printf("%02x ", (uint8_t)buf[i + j]);
        }
        vga_puts("\n");
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
    uint32_t ticks = timer_get_ticks();
    uint32_t seconds = ticks / 100;
    vga_printf("Boot time: %u seconds ago\n", seconds);
    vga_printf("Timer ticks: %u\n", ticks);
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
    vga_puts("DanyaOS v1.3.5\n");
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
    ipc_send(1, "Hello from shell!", 17);
    vga_puts("IPC test: message queued (pid 1).\n");
}

static void cmd_reboot(void) {
    vga_puts("Rebooting...\n");
    acpi_reboot();
    cli();
    hlt();
}

static void add_to_history(const char* cmd) {
    if (history_count < HISTORY_SIZE) {
        strncpy(history[history_count], cmd, CMD_BUF_SIZE - 1);
        history[history_count][CMD_BUF_SIZE - 1] = '\0';
        history_count++;
    } else {
        for (int i = 0; i < HISTORY_SIZE - 1; i++) {
            strcpy(history[i], history[i + 1]);
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
    else if (strcmp(cmd, "disk") == 0) cmd_disk();
    else if (strcmp(cmd, "fatls") == 0) cmd_fatls();
    else if (strncmp(cmd, "fatread", 7) == 0 && (cmd[7] == ' ' || cmd[7] == '\0')) cmd_fatread(cmd + 7);
    else if (strncmp(cmd, "fatwrite", 8) == 0 && (cmd[8] == ' ' || cmd[8] == '\0')) cmd_fatwrite(cmd + 8);
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
