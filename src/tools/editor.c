#include "editor.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"  // keyboard_getchar, keyboard_flush
#include "../drivers/timer.h"
#include "../include/io.h"
#include "../libc/string.h"
#include "../memory/heap.h"
#include "../fs/tmpfs.h"

#define EDITOR_W 80
#define EDITOR_H 25
#define STATUS_Y (EDITOR_H - 1)
#define CONTENT_H (EDITOR_H - 2)

static editor_t ed;

static void editor_draw(void) {
    // Title bar (row 0)
    vga_set_color(VGA_BLACK, VGA_LIGHT_CYAN);
    for (int i = 0; i < EDITOR_W; i++) vga_putchar_at(i, 0, ' ');
    vga_puts_at(2, 0, "DANO Editor v1.4");
    if (ed.filename[0]) {
        vga_puts_at(EDITOR_W - strlen(ed.filename) - 4, 0, ed.filename);
    }
    if (ed.modified) vga_putchar_at(EDITOR_W - 2, 0, '*');

    // Line numbers + content (rows 1..CONTENT_H)
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (int i = 0; i < CONTENT_H; i++) {
        int line_idx = i + ed.scroll_y;
        // Clear whole row
        for (int col = 0; col < EDITOR_W; col++)
            vga_putchar_at(col, i + 1, ' ');

        // Line number
        vga_set_color(VGA_DARK_GREY, VGA_BLACK);
        char lnum[8];
        itoa(line_idx + 1, lnum, 10);
        int lnum_len = strlen(lnum);
        int lnum_start = 4 - (lnum_len < 4 ? lnum_len : 3);
        for (int p = 0; lnum[p] && lnum_start + p < 4; p++)
            vga_putchar_at(lnum_start + p, i + 1, lnum[p]);

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
    if (ed.cursor_x > len) ed.cursor_x = len;

    // Shift right, but ensure we don't write past EDITOR_LINE_LEN-1
    int max_shift = EDITOR_LINE_LEN - 1;
    for (int i = len; i >= ed.cursor_x; i--) {
        int dst = i + 1;
        if (dst > max_shift) continue;
        line[dst] = line[i];
    }
    if (ed.cursor_x <= max_shift) {
        line[ed.cursor_x] = c;
    }
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

    int buf_size = EDITOR_LINE_LEN * EDITOR_MAX_LINES;
    char* save_buf = (char*)kmalloc(buf_size);
    if (!save_buf) {
        vga_puts_at(1, STATUS_Y, "Out of memory");
        return -1;
    }
    uint32_t pos = 0;

    for (int i = 0; i < ed.line_count; i++) {
        int len = strlen(ed.lines[i]);
        if (pos + len + 1 > (uint32_t)buf_size) break;
        if (len > 0) {
            memcpy(save_buf + pos, ed.lines[i], len);
            pos += len;
        }
        if (i < ed.line_count - 1) {
            save_buf[pos++] = '\n';
        }
    }

    tmpfs_delete(ed.filename);
    if (pos > 0) {
        tmpfs_write(ed.filename, save_buf, pos);
    }

    kfree(save_buf);

    ed.modified = 0;
    char msg[80];
    snprintf(msg, sizeof(msg), "Saved %d bytes to %.48s", pos, ed.filename);
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

        if (c == 0x11) { // Ctrl+Q
            ed.running = 0;
        } else if (c == 0x13) { // Ctrl+S
            editor_save();
        } else if (c == '\n') {
            editor_newline();
        } else if (c == '\b') {
            editor_delete_char();
        } else if (c == (char)KEY_UP) {
            if (ed.cursor_y > 0) {
                ed.cursor_y--;
                if (ed.cursor_x > (int)strlen(ed.lines[ed.cursor_y]))
                    ed.cursor_x = strlen(ed.lines[ed.cursor_y]);
            }
        } else if (c == (char)KEY_DOWN) {
            if (ed.cursor_y < ed.line_count - 1) {
                ed.cursor_y++;
                if (ed.cursor_x > (int)strlen(ed.lines[ed.cursor_y]))
                    ed.cursor_x = strlen(ed.lines[ed.cursor_y]);
            }
        } else if (c == (char)KEY_LEFT) {
            if (ed.cursor_x > 0) {
                ed.cursor_x--;
            } else if (ed.cursor_y > 0) {
                ed.cursor_y--;
                ed.cursor_x = strlen(ed.lines[ed.cursor_y]);
            }
        } else if (c == (char)KEY_RIGHT) {
            if (ed.lines[ed.cursor_y][ed.cursor_x] != '\0') {
                ed.cursor_x++;
            } else if (ed.cursor_y < ed.line_count - 1) {
                ed.cursor_y++;
                ed.cursor_x = 0;
            }
        } else if (c == (char)KEY_HOME) {
            ed.cursor_x = 0;
        } else if (c == (char)KEY_END) {
            ed.cursor_x = strlen(ed.lines[ed.cursor_y]);
        } else if (c == (char)KEY_PGUP) {
            int target = ed.cursor_y - CONTENT_H;
            if (target < 0) target = 0;
            ed.cursor_y = target;
            ed.scroll_y = (ed.cursor_y > CONTENT_H) ? ed.cursor_y - CONTENT_H + 1 : 0;
            if (ed.cursor_x > (int)strlen(ed.lines[ed.cursor_y]))
                ed.cursor_x = strlen(ed.lines[ed.cursor_y]);
        } else if (c == (char)KEY_PGDN) {
            int target = ed.cursor_y + CONTENT_H;
            if (target >= ed.line_count) target = ed.line_count - 1;
            if (target < 0) target = 0;
            ed.cursor_y = target;
            ed.scroll_y = ed.cursor_y;
            if (ed.cursor_y > ed.line_count - 1) ed.cursor_y = ed.line_count - 1;
            if (ed.cursor_x > (int)strlen(ed.lines[ed.cursor_y]))
                ed.cursor_x = strlen(ed.lines[ed.cursor_y]);
        } else if (c >= 32 && c < 127) {
            editor_insert_char(c);
        }

        // Scroll
        if (ed.cursor_y < ed.scroll_y) ed.scroll_y = ed.cursor_y;
        if (ed.cursor_y >= ed.scroll_y + CONTENT_H) ed.scroll_y = ed.cursor_y - CONTENT_H + 1;
    }

    keyboard_flush();
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}
