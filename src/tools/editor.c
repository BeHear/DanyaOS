#include "editor.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../include/io.h"
#include "../libc/string.h"
#include "../fs/tmpfs.h"

#define EDITOR_W 80
#define EDITOR_H 25
#define STATUS_Y (EDITOR_H - 1)
#define CONTENT_H (EDITOR_H - 3)

static editor_t ed;

static void editor_draw(void) {
    vga_clear();

    // Title bar
    vga_set_color(VGA_BLACK, VGA_LIGHT_CYAN);
    for (int i = 0; i < EDITOR_W; i++) vga_putchar_at(i, 0, ' ');
    vga_puts_at(2, 0, "DANO Editor v1.3.1");
    if (ed.filename[0]) {
        vga_puts_at(EDITOR_W - strlen(ed.filename) - 4, 0, ed.filename);
    }
    if (ed.modified) vga_putchar_at(EDITOR_W - 2, 0, '*');

    // Line numbers + content
    for (int i = 0; i < CONTENT_H; i++) {
        int line_idx = i + ed.scroll_y;
        vga_set_color(VGA_DARK_GREY, VGA_BLACK);
        char lnum[8];
        itoa(line_idx + 1, lnum, 10);
        int pad = 3 - strlen(lnum);
        char lbuf[8] = "   ";
        for (int p = 0; lnum[p]; p++) lbuf[3 - strlen(lnum) + p] = lnum[p];
        vga_puts_at(0, i + 1, lbuf);

        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        if (line_idx < ed.line_count) {
            vga_puts_at(5, i + 1, ed.lines[line_idx]);
        }
    }

    // Status bar
    vga_set_color(VGA_WHITE, VGA_BLUE);
    for (int i = 0; i < EDITOR_W; i++) vga_putchar_at(i, STATUS_Y, ' ');
    char status[64];
    strcpy(status, "Line ");
    char tmp[8];
    itoa(ed.cursor_y + 1, tmp, 10);
    strcat(status, tmp);
    strcat(status, "/");
    itoa(ed.line_count, tmp, 10);
    strcat(status, tmp);
    strcat(status, "  Col ");
    itoa(ed.cursor_x + 1, tmp, 10);
    strcat(status, tmp);
    vga_puts_at(1, STATUS_Y, status);
    vga_puts_at(EDITOR_W - 30, STATUS_Y, "Ctrl+S:Save Ctrl+Q:Quit");

    // Cursor
    int draw_y = ed.cursor_y - ed.scroll_y + 1;
    vga_set_color(VGA_BLACK, VGA_WHITE);
    if (ed.cursor_y < ed.line_count) {
        char c = ed.lines[ed.cursor_y][ed.cursor_x];
        if (c == '\0') c = ' ';
        vga_putchar_at(5 + ed.cursor_x, draw_y, c);
    }
}

static void editor_insert_char(char c) {
    if (ed.cursor_y >= ed.line_count) {
        if (ed.line_count >= EDITOR_MAX_LINES) return;
        ed.lines[ed.line_count][0] = '\0';
        ed.line_count++;
    }
    char* line = ed.lines[ed.cursor_y];
    int len = strlen(line);
    if (len >= EDITOR_LINE_LEN - 1) return;

    for (int i = len; i > ed.cursor_x; i--) {
        line[i + 1] = line[i];
    }
    line[ed.cursor_x] = c;
    ed.cursor_x++;
    ed.modified = 1;
}

static void editor_delete_char(void) {
    if (ed.cursor_x > 0) {
        char* line = ed.lines[ed.cursor_y];
        int len = strlen(line);
        for (int i = ed.cursor_x - 1; i < len; i++) {
            line[i] = line[i + 1];
        }
        ed.cursor_x--;
        ed.modified = 1;
    } else if (ed.cursor_y > 0) {
        int prev_len = strlen(ed.lines[ed.cursor_y - 1]);
        int cur_len = strlen(ed.lines[ed.cursor_y]);
        if (prev_len + cur_len < EDITOR_LINE_LEN) {
            strcat(ed.lines[ed.cursor_y - 1], ed.lines[ed.cursor_y]);
            for (int i = ed.cursor_y; i < ed.line_count - 1; i++) {
                strcpy(ed.lines[i], ed.lines[i + 1]);
            }
            ed.line_count--;
            ed.cursor_y--;
            ed.cursor_x = prev_len;
            ed.modified = 1;
        }
    }
}

static void editor_newline(void) {
    if (ed.line_count >= EDITOR_MAX_LINES) return;

    for (int i = ed.line_count; i > ed.cursor_y + 1; i--) {
        strcpy(ed.lines[i], ed.lines[i - 1]);
    }

    char* line = ed.lines[ed.cursor_y];
    strcpy(ed.lines[ed.cursor_y + 1], line + ed.cursor_x);
    line[ed.cursor_x] = '\0';

    ed.line_count++;
    ed.cursor_y++;
    ed.cursor_x = 0;
    ed.modified = 1;
}

static int editor_save(void) {
    if (!ed.filename[0]) {
        vga_puts_at(1, STATUS_Y, "No filename! Use: dano <filename>");
        return -1;
    }

    tmpfs_delete(ed.filename);

    uint32_t total = 0;
    for (int i = 0; i < ed.line_count; i++) {
        int len = strlen(ed.lines[i]);
        if (len > 0) {
            tmpfs_write(ed.filename, ed.lines[i], len);
            total += len;
        }
        if (i < ed.line_count - 1) {
            tmpfs_write(ed.filename, "\n", 1);
            total++;
        }
    }

    ed.modified = 0;
    char msg[64] = "Saved ";
    char tmp[8];
    itoa(total, tmp, 10);
    strcat(msg, tmp);
    strcat(msg, " bytes to ");
    strcat(msg, ed.filename);
    vga_puts_at(1, STATUS_Y, msg);
    return 0;
}

void editor_open(const char* filename) {
    memset(&ed, 0, sizeof(ed));
    strncpy(ed.filename, filename, EDITOR_NAME_LEN - 1);

    char buf[EDITOR_LINE_LEN];
    int len = tmpfs_read(filename, buf, EDITOR_LINE_LEN - 1);

    if (len > 0) {
        buf[len] = '\0';
        int line = 0;
        int col = 0;

        for (int i = 0; i < len && line < EDITOR_MAX_LINES; i++) {
            if (buf[i] == '\n') {
                ed.lines[line][col] = '\0';
                line++;
                col = 0;
            } else {
                if (col < EDITOR_LINE_LEN - 1) {
                    ed.lines[line][col++] = buf[i];
                }
            }
        }
        if (col > 0 || line == 0) {
            ed.lines[line][col] = '\0';
            line++;
        }
        ed.line_count = line;
    } else {
        ed.lines[0][0] = '\0';
        ed.line_count = 1;
    }

    ed.cursor_x = 0;
    ed.cursor_y = 0;
    ed.scroll_y = 0;
    ed.modified = 0;
    ed.running = 1;
}

void editor_new(void) {
    memset(&ed, 0, sizeof(ed));
    ed.lines[0][0] = '\0';
    ed.line_count = 1;
    ed.cursor_x = 0;
    ed.cursor_y = 0;
    ed.scroll_y = 0;
    ed.modified = 0;
    ed.running = 1;
}

void editor_run(void) {
    while (ed.running) {
        editor_draw();

        char c = keyboard_getchar();

        // Ctrl key combos (check extended scancodes)
        if (c == 0x11) { // Ctrl+Q
            ed.running = 0;
        } else if (c == 0x13) { // Ctrl+S
            editor_save();
        } else if (c == '\n') {
            editor_newline();
        } else if (c == '\b') {
            editor_delete_char();
        } else if (c >= 32 && c < 127) {
            editor_insert_char(c);
        }

        // Scroll
        if (ed.cursor_y < ed.scroll_y) ed.scroll_y = ed.cursor_y;
        if (ed.cursor_y >= ed.scroll_y + CONTENT_H) ed.scroll_y = ed.cursor_y - CONTENT_H + 1;
    }

    vga_clear();
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}
