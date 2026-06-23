#include "vga.h"
#include "../include/io.h"
#include "../libc/string.h"
#include <stdarg.h>

static uint16_t* vga_buffer;
static int vga_row;
static int vga_col;
static uint8_t vga_color_attr;
static int vga_available;

static inline uint8_t vga_entry_color(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

void vga_init(void) {
    vga_available = 0;

    // Test if VGA memory is accessible (may not be on UEFI)
    volatile uint16_t* test = (volatile uint16_t*)0xB8000;
    uint16_t saved = *test;
    *test = 0xA55A;
    if (*test == 0xA55A) {
        *test = saved;
        vga_available = 1;
    }

    if (!vga_available) return;

    vga_buffer = (uint16_t*)0xB8000;
    vga_row = 0;
    vga_col = 0;
    vga_color_attr = vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK);

    outb(0x3D4, 0x0C);
    outb(0x3D5, 0x00);
    outb(0x3D4, 0x0D);
    outb(0x3D5, 0x00);

    vga_clear();

    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

void vga_clear(void) {
    if (!vga_available) return;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = vga_entry(' ', vga_color_attr);
    }
    vga_row = 0;
    vga_col = 0;

    outb(0x3D4, 0x0F);
    outb(0x3D5, 0x00);
    outb(0x3D4, 0x0E);
    outb(0x3D5, 0x00);
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    vga_color_attr = vga_entry_color(fg, bg);
}

void vga_scroll(void) {
    if (!vga_available) return;
    if (vga_row >= VGA_HEIGHT) {
        for (int i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++) {
            vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
        }
        for (int i = VGA_WIDTH * (VGA_HEIGHT - 1); i < VGA_WIDTH * VGA_HEIGHT; i++) {
            vga_buffer[i] = vga_entry(' ', vga_color_attr);
        }
        vga_row = VGA_HEIGHT - 1;
    }
}

void vga_putchar(char c) {
    if (!vga_available) return;
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\r') {
        vga_col = 0;
    } else if (c == '\t') {
        vga_col = (vga_col + 8) & ~7;
    } else if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
            vga_buffer[vga_row * VGA_WIDTH + vga_col] = vga_entry(' ', vga_color_attr);
        } else if (vga_row > 0) {
            vga_row--;
            vga_col = VGA_WIDTH - 1;
            vga_buffer[vga_row * VGA_WIDTH + vga_col] = vga_entry(' ', vga_color_attr);
        }
    } else {
        vga_buffer[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, vga_color_attr);
        vga_col++;
    }

    if (vga_col >= VGA_WIDTH) {
        vga_col = 0;
        vga_row++;
    }

    vga_scroll();

    uint16_t cursor = vga_row * VGA_WIDTH + vga_col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(cursor & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((cursor >> 8) & 0xFF));
}

void vga_putchar_at(int x, int y, char c) {
    if (x < 0 || x >= VGA_WIDTH || y < 0 || y >= VGA_HEIGHT) return;
    vga_buffer[y * VGA_WIDTH + x] = vga_entry(c, vga_color_attr);
}

void vga_puts_at(int x, int y, const char* s) {
    while (*s) {
        if (x >= VGA_WIDTH) break;
        vga_putchar_at(x, y, *s++);
        x++;
    }
}

void vga_puts(const char* s) {
    while (*s) {
        vga_putchar(*s++);
    }
}

static void print_fmt_int(int val, int width, int left_align, char pad_char) {
    char buf[32];
    itoa(val, buf, 10);
    int len = strlen(buf);
    int pad = (width > len) ? width - len : 0;
    if (!left_align) {
        while (pad-- > 0) vga_putchar(pad_char);
    }
    vga_puts(buf);
    if (left_align) {
        while (pad-- > 0) vga_putchar(' ');
    }
}

static void print_fmt_uint(uint32_t val, int width, int left_align, char pad_char) {
    char buf[32];
    uitoa(val, buf, 10);
    int len = strlen(buf);
    int pad = (width > len) ? width - len : 0;
    if (!left_align) {
        while (pad-- > 0) vga_putchar(pad_char);
    }
    vga_puts(buf);
    if (left_align) {
        while (pad-- > 0) vga_putchar(' ');
    }
}

static void print_fmt_hex(uint32_t val, int width, int left_align, char pad_char) {
    char buf[32];
    uitoa(val, buf, 16);
    int len = strlen(buf);
    int pad = (width > len) ? width - len : 0;
    if (!left_align) {
        while (pad-- > 0) vga_putchar(pad_char);
    }
    vga_puts(buf);
    if (left_align) {
        while (pad-- > 0) vga_putchar(' ');
    }
}

static void print_fmt_str(const char* s, int width, int left_align) {
    if (!s) s = "(null)";
    int len = strlen(s);
    int pad = (width > len) ? width - len : 0;
    if (!left_align) {
        while (pad-- > 0) vga_putchar(' ');
    }
    vga_puts(s);
    if (left_align) {
        while (pad-- > 0) vga_putchar(' ');
    }
}

void vga_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == '\0') break;

            int left_align = 0;
            char pad_char = ' ';
            int width = 0;

            if (*fmt == '-') {
                left_align = 1;
                fmt++;
            }
            if (*fmt == '0') {
                pad_char = '0';
                fmt++;
            }
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }

            switch (*fmt) {
                case 's': {
                    const char* s = va_arg(args, const char*);
                    print_fmt_str(s, width, left_align);
                    break;
                }
                case 'd': {
                    int val = va_arg(args, int);
                    print_fmt_int(val, width, left_align, pad_char);
                    break;
                }
                case 'u': {
                    uint32_t val = va_arg(args, uint32_t);
                    print_fmt_uint(val, width, left_align, pad_char);
                    break;
                }
                case 'x': {
                    uint32_t val = va_arg(args, uint32_t);
                    print_fmt_hex(val, width, left_align, pad_char);
                    break;
                }
                case 'p': {
                    uint32_t val = va_arg(args, uint32_t);
                    vga_puts("0x");
                    print_fmt_hex(val, width, left_align, pad_char);
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    vga_putchar(c);
                    break;
                }
                case '%': {
                    vga_putchar('%');
                    break;
                }
                default:
                    vga_putchar('%');
                    vga_putchar(*fmt);
                    break;
            }
        } else {
            vga_putchar(*fmt);
        }
        fmt++;
    }

    va_end(args);
}
