#ifndef DANYA_SCHEDULER_H
#define DANYA_SCHEDULER_H

#include "../include/types.h"

#define MAX_PROCESSES 32
#define KERNEL_STACK_SIZE 4096

typedef enum {
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_ZOMBIE,
} proc_state_t;

typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip, eflags;
    uint32_t cs, ss, ds, es, fs, gs;
} __attribute__((packed)) cpu_state_t;

typedef struct {
    pid_t       pid;
    proc_state_t state;
    char        name[32];
    uint32_t    kernel_stack;
    uint32_t    user_stack;
    cpu_state_t cpu_state;
    uint32_t    time_slices;
    int32_t     exit_code;
} process_t;

void     scheduler_init(void);
void     scheduler_tick(void);
void     process_create(const char* name, void (*entry)(void));
void     process_exit(int code);
void     process_yield(void);
process_t* scheduler_current(void);
process_t* scheduler_get(pid_t pid);
int      scheduler_process_count(void);

#endif
