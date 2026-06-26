#include "keyboard.h"
#include "../include/io.h"
#include "../kernel/idt.h"

static volatile char key_buffer;
static volatile bool key_available = false;
static volatile uint8_t scancode_buffer = 0;
static volatile bool scancode_available = false;

static const char scancode_to_ascii[] = {
    0, 0, '1', '2', '3', '4', '5', '6',
    '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o', 'p', '[', ']', '\n', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*',
    0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, '-', 0, 0, 0, '+', 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
};

static const char scancode_to_shift[] = {
    0, 0, '!', '@', '#', '$', '%', '^',
    '&', '*', '(', ')', '_', '+', 0, 0,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '{', '}', '\n', 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*',
    0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, '-', 0, 0, 0, '+', 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
};

static uint8_t shift_pressed = 0;
static uint8_t ctrl_pressed = 0;
static uint8_t caps_lock = 0;
static uint8_t extended_prefix = 0;

static void keyboard_handler(stack_state_t* state) {
    UNUSED(state);
    uint8_t scancode = inb(0x60);

    if (scancode == 0xE0) {
        extended_prefix = 1;
        return;
    }

    bool is_extended = false;
    if (extended_prefix) {
        is_extended = true;
        extended_prefix = 0;
    }

    if (scancode == 0x1D) { ctrl_pressed = 1; return; }
    if (scancode == 0x9D) { ctrl_pressed = 0; return; }

    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        return;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = 0;
        return;
    }

    if (scancode == 0x3A) {
        caps_lock = !caps_lock;
        return;
    }

    if (scancode & 0x80) return;

    scancode_buffer = scancode;
    scancode_available = true;

    if (is_extended) return;

    char c = shift_pressed ? scancode_to_shift[scancode] : scancode_to_ascii[scancode];

    if (ctrl_pressed && c >= 'a' && c <= 'z') {
        c &= 0x1F;
    } else if (ctrl_pressed && c >= 'A' && c <= 'Z') {
        c &= 0x1F;
    } else if (caps_lock) {
        if (c >= 'a' && c <= 'z')
            c = c - 'a' + 'A';
        else if (c >= 'A' && c <= 'Z')
            c = c - 'A' + 'a';
    }

    if (c) {
        key_buffer = c;
        key_available = true;
    }
}

void keyboard_init(void) {
    idt_register_handler(33, keyboard_handler);
    uint8_t mask = inb(0x21);
    mask &= ~(1 << 1);
    outb(0x21, mask);
}

char keyboard_getchar(void) {
    while (!key_available) hlt();
    cli();
    char c = key_buffer;
    key_available = false;
    sti();
    return c;
}

bool keyboard_has_key(void) {
    return key_available;
}

uint8_t keyboard_get_scancode(void) {
    while (!scancode_available) hlt();
    cli();
    uint8_t sc = scancode_buffer;
    scancode_available = false;
    sti();
    return sc;
}

void keyboard_flush(void) {
    cli();
    key_available = false;
    scancode_available = false;
    sti();
}
