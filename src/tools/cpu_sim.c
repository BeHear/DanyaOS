#include "cpu_sim.h"
#include "../drivers/vga.h"
#include "../libc/string.h"

static cpu_sim_state_t cpu;

void cpu_sim_init(void) {
    cpu_sim_reset();
}

void cpu_sim_reset(void) {
    memset(&cpu, 0, sizeof(cpu));
    cpu.esp = 0xFF;
    cpu.eflags = 0x00000002;
    memset(cpu.memory, 0, 256);
}

uint32_t cpu_sim_get_reg(const char* name) {
    if (strcmp(name, "eax") == 0) return cpu.eax;
    if (strcmp(name, "ebx") == 0) return cpu.ebx;
    if (strcmp(name, "ecx") == 0) return cpu.ecx;
    if (strcmp(name, "edx") == 0) return cpu.edx;
    if (strcmp(name, "esi") == 0) return cpu.esi;
    if (strcmp(name, "edi") == 0) return cpu.edi;
    if (strcmp(name, "ebp") == 0) return cpu.ebp;
    if (strcmp(name, "esp") == 0) return cpu.esp;
    if (strcmp(name, "eip") == 0) return cpu.eip;
    if (strcmp(name, "eflags") == 0 || strcmp(name, "flags") == 0) return cpu.eflags;
    return 0;
}

void cpu_sim_set_reg(const char* name, uint32_t value) {
    if (strcmp(name, "eax") == 0) cpu.eax = value;
    else if (strcmp(name, "ebx") == 0) cpu.ebx = value;
    else if (strcmp(name, "ecx") == 0) cpu.ecx = value;
    else if (strcmp(name, "edx") == 0) cpu.edx = value;
    else if (strcmp(name, "esi") == 0) cpu.esi = value;
    else if (strcmp(name, "edi") == 0) cpu.edi = value;
    else if (strcmp(name, "ebp") == 0) cpu.ebp = value;
    else if (strcmp(name, "esp") == 0) cpu.esp = value;
    else if (strcmp(name, "eip") == 0) cpu.eip = value;
    else if (strcmp(name, "eflags") == 0 || strcmp(name, "flags") == 0) cpu.eflags = value;
}

static void update_flags(uint32_t result) {
    cpu.eflags &= ~0x8D5;  // clear CF, ZF, SF, OF, PF
    if (result == 0) cpu.eflags |= 0x40;         // ZF
    if (result & 0x80000000) cpu.eflags |= 0x80; // SF
    uint8_t p = (uint8_t)result;
    p ^= p >> 4; p ^= p >> 2; p ^= p >> 1;
    if (!(p & 1)) cpu.eflags |= 0x04;            // PF
}

static uint32_t* find_reg(const char* name) {
    if (strcmp(name, "eax") == 0) return &cpu.eax;
    if (strcmp(name, "ebx") == 0) return &cpu.ebx;
    if (strcmp(name, "ecx") == 0) return &cpu.ecx;
    if (strcmp(name, "edx") == 0) return &cpu.edx;
    if (strcmp(name, "esi") == 0) return &cpu.esi;
    if (strcmp(name, "edi") == 0) return &cpu.edi;
    if (strcmp(name, "ebp") == 0) return &cpu.ebp;
    if (strcmp(name, "esp") == 0) return &cpu.esp;
    if (strcmp(name, "eip") == 0) return &cpu.eip;
    return NULL;
}

static int parse_operand(const char* s, uint32_t* out) {
    while (*s == ' ') s++;
    if (*s == '[') {
        s++;
        uint32_t addr = 0;
        if (s[0] == '0' && s[1] == 'x') {
            s += 2;
            while (*s && *s != ']') {
                char c = *s++;
                addr <<= 4;
                if (c >= '0' && c <= '9') addr += c - '0';
                else if (c >= 'a' && c <= 'f') addr += c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') addr += c - 'A' + 10;
            }
        } else {
            while (*s && *s != ']') {
                char c = *s++;
                if (c >= '0' && c <= '9') { addr *= 10; addr += c - '0'; }
            }
        }
        *out = addr;
        return 2;  // memory operand
    }
    uint32_t* reg = find_reg(s);
    if (reg) { *out = *reg; return 1; }  // register
    // immediate value
    *out = 0;
    if (s[0] == '0' && s[1] == 'x') {
        s += 2;
        while (*s) {
            char c = *s++;
            *out <<= 4;
            if (c >= '0' && c <= '9') *out += c - '0';
            else if (c >= 'a' && c <= 'f') *out += c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') *out += c - 'A' + 10;
        }
    } else {
        int neg = 0;
        if (*s == '-') { neg = 1; s++; }
        while (*s >= '0' && *s <= '9') { *out = *out * 10 + (*s - '0'); s++; }
        if (neg) *out = -*out;
    }
    return 0;  // immediate
}

static void set_operand(const char* s, uint32_t val) {
    while (*s == ' ') s++;
    if (*s == '[') return;  // can't set memory from here directly
    uint32_t* reg = find_reg(s);
    if (reg) *reg = val;
}

void cpu_sim_dump(void) {
    vga_printf("  EAX=%08x  EBX=%08x\n", cpu.eax, cpu.ebx);
    vga_printf("  ECX=%08x  EDX=%08x\n", cpu.ecx, cpu.edx);
    vga_printf("  ESI=%08x  EDI=%08x\n", cpu.esi, cpu.edi);
    vga_printf("  EBP=%08x  ESP=%08x\n", cpu.ebp, cpu.esp);
    vga_printf("  EIP=%08x\n", cpu.eip);
    vga_printf("  FLAGS: ZF=%d SF=%d CF=%d OF=%d PF=%d\n",
               (cpu.eflags >> 6) & 1, (cpu.eflags >> 7) & 1,
               (cpu.eflags >> 0) & 1, (cpu.eflags >> 11) & 1,
               (cpu.eflags >> 2) & 1);
}

void cpu_sim_mem_dump(uint32_t addr, int count) {
    for (int i = 0; i < count; i += 16) {
        vga_printf("  %04x: ", addr + i);
        for (int j = 0; j < 16 && (i + j) < count; j++) {
            vga_printf("%02x ", cpu.memory[(addr + i + j) & 0xFF]);
        }
        vga_puts("\n");
    }
}

int cpu_sim_execute(const char* instruction) {
    char op[16];
    char operand1[32];
    char operand2[32];
    int i = 0;

    while (*instruction && *instruction != ' ' && i < 15) op[i++] = *instruction++;
    op[i] = '\0';

    while (*instruction == ' ') instruction++;
    i = 0;
    while (*instruction && *instruction != ',' && *instruction != ' ' && i < 31) operand1[i++] = *instruction++;
    operand1[i] = '\0';

    while (*instruction == ' ' || *instruction == ',') instruction++;
    i = 0;
    while (*instruction && *instruction != '\0' && i < 31) operand2[i++] = *instruction++;
    operand2[i] = '\0';

    uint32_t op1_val, op2_val;
    int op1_type = parse_operand(operand1, &op1_val);
    int op2_type = parse_operand(operand2, &op2_val);

    cpu.eip++;

    if (strcmp(op, "mov") == 0) {
        set_operand(operand1, op2_val);
        vga_printf("  MOV %s, 0x%x\n", operand1, op2_val);
    } else if (strcmp(op, "add") == 0) {
        uint32_t result = op1_val + op2_val;
        update_flags(result);
        set_operand(operand1, result);
        vga_printf("  ADD %s, %s => 0x%x\n", operand1, operand2, result);
    } else if (strcmp(op, "sub") == 0) {
        uint32_t result = op1_val - op2_val;
        update_flags(result);
        if (op1_val < op2_val) cpu.eflags |= 1; // CF
        set_operand(operand1, result);
        vga_printf("  SUB %s, %s => 0x%x\n", operand1, operand2, result);
    } else if (strcmp(op, "mul") == 0) {
        uint64_t result = (uint64_t)op1_val * (uint64_t)op2_val;
        cpu.eax = (uint32_t)result;
        cpu.edx = (uint32_t)(result >> 32);
        vga_printf("  MUL %s, %s => EDX:EAX = 0x%08x%08x\n", operand1, operand2, cpu.edx, cpu.eax);
    } else if (strcmp(op, "div") == 0) {
        if (op2_val == 0) { vga_puts("  ERROR: division by zero\n"); return -1; }
        uint32_t quotient = op1_val / op2_val;
        uint32_t remainder = op1_val % op2_val;
        cpu.eax = quotient;
        cpu.edx = remainder;
        vga_printf("  DIV %s, %s => EAX=%x EDX=%x\n", operand1, operand2, quotient, remainder);
    } else if (strcmp(op, "and") == 0) {
        uint32_t result = op1_val & op2_val;
        update_flags(result);
        set_operand(operand1, result);
        vga_printf("  AND %s, %s => 0x%x\n", operand1, operand2, result);
    } else if (strcmp(op, "or") == 0) {
        uint32_t result = op1_val | op2_val;
        update_flags(result);
        set_operand(operand1, result);
        vga_printf("  OR %s, %s => 0x%x\n", operand1, operand2, result);
    } else if (strcmp(op, "xor") == 0) {
        uint32_t result = op1_val ^ op2_val;
        update_flags(result);
        set_operand(operand1, result);
        vga_printf("  XOR %s, %s => 0x%x\n", operand1, operand2, result);
    } else if (strcmp(op, "not") == 0) {
        uint32_t result = ~op1_val;
        set_operand(operand1, result);
        vga_printf("  NOT %s => 0x%x\n", operand1, result);
    } else if (strcmp(op, "shl") == 0) {
        uint32_t result = op1_val << op2_val;
        update_flags(result);
        set_operand(operand1, result);
        vga_printf("  SHL %s, %s => 0x%x\n", operand1, operand2, result);
    } else if (strcmp(op, "shr") == 0) {
        uint32_t result = op1_val >> op2_val;
        update_flags(result);
        set_operand(operand1, result);
        vga_printf("  SHR %s, %s => 0x%x\n", operand1, operand2, result);
    } else if (strcmp(op, "inc") == 0) {
        uint32_t result = op1_val + 1;
        update_flags(result);
        set_operand(operand1, result);
        vga_printf("  INC %s => 0x%x\n", operand1, result);
    } else if (strcmp(op, "dec") == 0) {
        uint32_t result = op1_val - 1;
        update_flags(result);
        set_operand(operand1, result);
        vga_printf("  DEC %s => 0x%x\n", operand1, result);
    } else if (strcmp(op, "cmp") == 0) {
        uint32_t result = op1_val - op2_val;
        update_flags(result);
        if (op1_val < op2_val) cpu.eflags |= 1;
        vga_printf("  CMP %s, %s => ZF=%d SF=%d CF=%d\n", operand1, operand2,
                   (cpu.eflags >> 6) & 1, (cpu.eflags >> 7) & 1, cpu.eflags & 1);
    } else if (strcmp(op, "test") == 0) {
        uint32_t result = op1_val & op2_val;
        update_flags(result);
        vga_printf("  TEST %s, %s => ZF=%d SF=%d\n", operand1, operand2,
                   (cpu.eflags >> 6) & 1, (cpu.eflags >> 7) & 1);
    } else if (strcmp(op, "push") == 0) {
        cpu.esp -= 4;
        cpu.memory[cpu.esp & 0xFF] = op1_val & 0xFF;
        cpu.memory[(cpu.esp + 1) & 0xFF] = (op1_val >> 8) & 0xFF;
        cpu.memory[(cpu.esp + 2) & 0xFF] = (op1_val >> 16) & 0xFF;
        cpu.memory[(cpu.esp + 3) & 0xFF] = (op1_val >> 24) & 0xFF;
        vga_printf("  PUSH 0x%x => [ESP]=0x%x, ESP=%x\n", op1_val, op1_val, cpu.esp);
    } else if (strcmp(op, "pop") == 0) {
        uint32_t val = cpu.memory[cpu.esp & 0xFF] |
                       (cpu.memory[(cpu.esp + 1) & 0xFF] << 8) |
                       (cpu.memory[(cpu.esp + 2) & 0xFF] << 16) |
                       (cpu.memory[(cpu.esp + 3) & 0xFF] << 24);
        cpu.esp += 4;
        set_operand(operand1, val);
        vga_printf("  POP %s => 0x%x, ESP=%x\n", operand1, val, cpu.esp);
    } else if (strcmp(op, "nop") == 0) {
        vga_puts("  NOP\n");
    } else {
        vga_printf("  Unknown instruction: %s\n", op);
        return -1;
    }
    return 0;
}
