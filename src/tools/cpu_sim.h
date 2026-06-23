#ifndef DANYA_CPU_SIM_H
#define DANYA_CPU_SIM_H

#include "../include/types.h"

typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip;
    uint32_t eflags;
    uint8_t  memory[256];
} cpu_sim_state_t;

void cpu_sim_init(void);
void cpu_sim_reset(void);
uint32_t cpu_sim_get_reg(const char* name);
void cpu_sim_set_reg(const char* name, uint32_t value);
void cpu_sim_dump(void);
int  cpu_sim_execute(const char* instruction);
void cpu_sim_mem_dump(uint32_t addr, int count);

#endif
