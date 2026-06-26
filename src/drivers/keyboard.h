#ifndef DANYA_KEYBOARD_H
#define DANYA_KEYBOARD_H

#include "../kernel/idt.h"

void keyboard_init(void);
char keyboard_getchar(void);
bool keyboard_has_key(void);
uint8_t keyboard_get_scancode(void);
bool keyboard_has_scancode(void);
void keyboard_flush(void);

// Extended key codes returned by keyboard_get_extended()
#define KEY_NONE     0x00
#define KEY_UP       0xF1
#define KEY_DOWN     0xF2
#define KEY_LEFT     0xF3
#define KEY_RIGHT    0xF4
#define KEY_HOME     0xF5
#define KEY_END      0xF6
#define KEY_PGUP     0xF7
#define KEY_PGDN     0xF8
#define KEY_DEL      0xF9
#define KEY_INS      0xFA

// Read next extended key (arrow, home/end, pgup/pgdn). Returns KEY_NONE if not available.
uint8_t keyboard_get_extended(void);

#endif
