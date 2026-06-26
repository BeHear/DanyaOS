#include "tui.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../memory/heap.h"
#include "../libc/string.h"

#define TUI_W 80
#define TUI_H 25
#define MENU_ITEMS 5

#define SCAN_UP     0x48
#define SCAN_DOWN   0x50
#define SCAN_LEFT   0x4B
#define SCAN_RIGHT  0x4D
#define SCAN_ENTER  0x1C
#define SCAN_ESC    0x01

static const char* menu_items[MENU_ITEMS] = {
    "Memory Info",
    "Process List",
    "File Manager",
    "System Info",
    "Exit"
};

static int selected = 0;

static void draw_box(int x, int y, int w, int h, uint8_t fg, uint8_t bg) {
    vga_set_color(fg, bg);
    for (int i = y; i < y + h; i++) {
        for (int j = x; j < x + w; j++) {
            if (i == y && j == x) vga_putchar_at(j, i, 0xDA);
            else if (i == y && j == x + w - 1) vga_putchar_at(j, i, 0xBF);
            else if (i == y + h - 1 && j == x) vga_putchar_at(j, i, 0xC0);
            else if (i == y + h - 1 && j == x + w - 1) vga_putchar_at(j, i, 0xD9);
            else if (i == y || i == y + h - 1) vga_putchar_at(j, i, 0xC4);
            else if (j == x || j == x + w - 1) vga_putchar_at(j, i, 0xB3);
            else vga_putchar_at(j, i, ' ');
        }
    }
}

static void draw_hline(int x, int y, int len, uint8_t fg, uint8_t bg) {
    vga_set_color(fg, bg);
    vga_putchar_at(x, y, 0xC4);
    for (int i = 1; i < len - 1; i++) vga_putchar_at(x + i, y, 0xC4);
    vga_putchar_at(x + len - 1, y, 0xC4);
}

static void draw_title(int x, int y, const char* title, int w, uint8_t fg, uint8_t bg) {
    int len = strlen(title);
    int pad = (w - len - 2) / 2;
    vga_set_color(fg, bg);
    vga_putchar_at(x + pad, y, ' ');
    vga_puts_at(x + pad + 1, y, title);
    vga_putchar_at(x + pad + 1 + len, y, ' ');
}

static void draw_status_bar(const char* text, uint8_t fg, uint8_t bg) {
    vga_set_color(fg, bg);
    for (int i = 0; i < TUI_W; i++) {
        vga_putchar_at(i, TUI_H - 1, ' ');
    }
    vga_puts_at(1, TUI_H - 1, text);
}

static void clear_area(int x, int y, int w, int h, uint8_t bg) {
    vga_set_color(VGA_LIGHT_GREY, bg);
    for (int i = y; i < y + h; i++) {
        for (int j = x; j < x + w; j++) {
            vga_putchar_at(j, i, ' ');
        }
    }
}

static void draw_menu(void) {
    int mx = 20, my = 3, mw = 40, mh = 20;

    clear_area(mx - 1, my - 1, mw + 2, mh + 2, VGA_BLACK);
    draw_box(mx, my, mw, mh, VGA_WHITE, VGA_BLUE);
    draw_title(mx, my, " DanyaOS TUI Test ", mw, VGA_LIGHT_CYAN, VGA_BLUE);
    draw_hline(mx, my + 2, mw, VGA_WHITE, VGA_BLUE);

    vga_set_color(VGA_LIGHT_GREY, VGA_BLUE);
    vga_puts_at(mx + 3, my + 4, "Select an option with UP/DOWN");
    vga_puts_at(mx + 3, my + 5, "Press ENTER to choose, ESC to exit");

    draw_hline(mx, my + 7, mw, VGA_WHITE, VGA_BLUE);

    for (int i = 0; i < MENU_ITEMS; i++) {
        int iy = my + 9 + i * 2;
        if (iy >= my + mh - 2) break;

        if (i == selected) {
            vga_set_color(VGA_BLACK, VGA_LIGHT_GREEN);
            for (int j = mx + 2; j < mx + mw - 2; j++) {
                vga_putchar_at(j, iy, ' ');
            }
            vga_puts_at(mx + 4, iy, ">> ");
            vga_puts_at(mx + 7, iy, menu_items[i]);
        } else {
            vga_set_color(VGA_WHITE, VGA_BLUE);
            for (int j = mx + 2; j < mx + mw - 2; j++) {
                vga_putchar_at(j, iy, ' ');
            }
            vga_puts_at(mx + 7, iy, menu_items[i]);
        }
    }

    draw_status_bar(" [UP/DOWN] Navigate  [ENTER] Select  [ESC] Exit ", VGA_WHITE, VGA_DARK_GREY);
}

static void show_info_box(const char* title, const char* lines[], int count) {
    int bx = 15, by = 3, bw = 50, bh = count + 6;

    clear_area(bx - 1, by - 1, bw + 2, bh + 2, VGA_BLACK);
    draw_box(bx, by, bw, bh, VGA_WHITE, VGA_DARK_GREY);
    draw_title(bx, by, title, bw, VGA_LIGHT_CYAN, VGA_DARK_GREY);

    for (int i = 0; i < count; i++) {
        vga_set_color(VGA_LIGHT_GREY, VGA_DARK_GREY);
        vga_puts_at(bx + 3, by + 3 + i, lines[i]);
    }

    draw_status_bar(" Press any key to return to menu ", VGA_WHITE, VGA_DARK_GREY);
}

static void show_memory_info(void) {
    const char* lines[] = {
        "Physical Memory Manager",
        "Total pages tracked by PMM",
        "Heap allocator active",
        "Virtual memory mapped"
    };
    show_info_box(" Memory Info ", lines, 4);
}

static void show_process_info(void) {
    const char* lines[] = {
        "Scheduler: Round-Robin",
        "IPC: Message passing",
        "Max processes: 32",
        "Timer: 100Hz tick"
    };
    show_info_box(" Process Info ", lines, 4);
}

static void show_file_info(void) {
    const char* lines[] = {
        "Filesystem: tmpfs (RAM-based)",
        "Supports: create, read, write",
        "Supports: delete, copy, move",
        "Max file size: 4096 bytes"
    };
    show_info_box(" File Manager ", lines, 4);
}

static void show_system_info(void) {
    const char* lines[] = {
        "DanyaOS v1.3.5 (Microkernel)",
        "Architecture: i386 (x86)",
        "VGA text mode 80x25",
        "Timer ticks active"
    };
    show_info_box(" System Info ", lines, 4);
}

void tui_test(void) {
    keyboard_flush();
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
    selected = 0;

    while (1) {
        draw_menu();

        uint8_t sc = keyboard_get_scancode();
        keyboard_flush();

        if (sc == SCAN_ESC) {
            break;
        } else if (sc == SCAN_UP) {
            if (selected > 0) selected--;
        } else if (sc == SCAN_DOWN) {
            if (selected < MENU_ITEMS - 1) selected++;
        } else if (sc == SCAN_ENTER) {
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            vga_clear();
            switch (selected) {
                case 0: show_memory_info(); break;
                case 1: show_process_info(); break;
                case 2: show_file_info(); break;
                case 3: show_system_info(); break;
                case 4:
                    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
                    vga_clear();
                    return;
            }
            keyboard_get_scancode();
            keyboard_flush();
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            vga_clear();
        }
    }

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

