#include "timer.h"
#include "../include/io.h"
#include "../kernel/idt.h"

static volatile uint32_t timer_ticks = 0;
extern void scheduler_tick(void);

static void timer_handler(stack_state_t* state) {
    UNUSED(state);
    timer_ticks++;
    scheduler_tick();
}

void timer_init(uint32_t freq) {
    idt_register_handler(32, timer_handler);

    uint32_t divisor = 1193180 / freq;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));

    uint8_t mask = inb(0x21);
    mask &= ~(1 << 0);
    outb(0x21, mask);
}

uint32_t timer_get_ticks(void) {
    cli();
    uint32_t t = timer_ticks;
    sti();
    return t;
}
