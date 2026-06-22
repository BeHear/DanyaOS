#include "idt.h"
#include "../include/io.h"
#include "../drivers/vga.h"
#include "../libc/string.h"

static idt_entry_t idt[256];
static idt_ptr_t   idt_ptr;

static isr_handler_t isr_handlers[256];

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel       = sel;
    idt[num].always0   = 0;
    idt[num].flags     = flags;
}

static void pic_remap(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11);
    io_wait();
    outb(0x21, 0x20); outb(0xA1, 0x28);
    io_wait();
    outb(0x21, 0x04); outb(0xA1, 0x02);
    io_wait();
    outb(0x21, 0x01); outb(0xA1, 0x01);
    io_wait();
    outb(0x21, 0x00); outb(0xA1, 0x00);
    io_wait();
}

void idt_init(void) {
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint32_t)&idt;

    memset(&idt, 0, sizeof(idt_entry_t) * 256);
    memset(&isr_handlers, 0, sizeof(isr_handler_t) * 256);

    pic_remap();

    idt_set_gate(0,  (uint32_t)isr0,  0x08, 0x8E);
    idt_set_gate(1,  (uint32_t)isr1,  0x08, 0x8E);
    idt_set_gate(2,  (uint32_t)isr2,  0x08, 0x8E);
    idt_set_gate(3,  (uint32_t)isr3,  0x08, 0x8E);
    idt_set_gate(4,  (uint32_t)isr4,  0x08, 0x8E);
    idt_set_gate(5,  (uint32_t)isr5,  0x08, 0x8E);
    idt_set_gate(6,  (uint32_t)isr6,  0x08, 0x8E);
    idt_set_gate(7,  (uint32_t)isr7,  0x08, 0x8E);
    idt_set_gate(8,  (uint32_t)isr8,  0x08, 0x8E);
    idt_set_gate(9,  (uint32_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);

    idt_set_gate(32, (uint32_t)isr32, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)isr33, 0x08, 0x8E);

    idt_set_gate(128, (uint32_t)isr128, 0x08, 0xEE);

    idt_flush((uint32_t)&idt_ptr);
}

void idt_register_handler(uint8_t num, isr_handler_t handler) {
    isr_handlers[num] = handler;
}

void isr_handler(stack_state_t* state) {
    if (isr_handlers[state->int_no]) {
        isr_handlers[state->int_no](state);
    } else if (state->int_no < 32) {
        vga_printf("\n!!! EXCEPTION %d (err=%x) at %p !!!\n",
                   state->int_no, state->err_code, state->eip);
        cli();
        hlt();
    }
}

void irq_handler(stack_state_t* state) {
    if (isr_handlers[state->int_no]) {
        isr_handlers[state->int_no](state);
    }

    if (state->int_no >= 40) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}
