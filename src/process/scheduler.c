#include "scheduler.h"
#include "../memory/pmm.h"
#include "../memory/vmm.h"
#include "../memory/heap.h"
#include "../libc/string.h"
#include "../include/io.h"
#include "../drivers/vga.h"

static process_t processes[MAX_PROCESSES];
static int current_process = -1;
static int process_count = 0;

void scheduler_init(void) {
    memset(processes, 0, sizeof(processes));
    current_process = -1;
}

void process_create(const char* name, void (*entry)(void)) {
    if (process_count >= MAX_PROCESSES) return;

    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_UNUSED) {
            slot = i;
            break;
        }
    }

    if (slot == -1) return;

    process_t* proc = &processes[slot];
    proc->pid = slot + 1;
    proc->state = PROC_READY;
    proc->time_slices = 0;
    proc->exit_code = 0;
    strncpy(proc->name, name, 31);
    proc->name[31] = '\0';

    void* kstack_page = pmm_alloc_page();
    if (!kstack_page) {
        proc->state = PROC_UNUSED;
        vga_printf("[scheduler] OOM creating process '%s'\n", name);
        return;
    }
    proc->kernel_stack = (uint32_t)kstack_page + PAGE_SIZE;
    proc->user_stack = 0xBFFFF000;

    void* ustack_page = pmm_alloc_page();
    if (!ustack_page) {
        pmm_free_page(kstack_page);
        proc->state = PROC_UNUSED;
        vga_printf("[scheduler] OOM creating process '%s'\n", name);
        return;
    }
    vmm_map_page(proc->user_stack, (uint32_t)ustack_page, 0x7);

    memset(&proc->cpu_state, 0, sizeof(cpu_state_t));
    proc->cpu_state.eip = (uint32_t)entry;
    proc->cpu_state.esp = proc->user_stack;
    proc->cpu_state.ebp = proc->user_stack;
    proc->cpu_state.cs  = 0x1B;
    proc->cpu_state.ds  = 0x23;
    proc->cpu_state.ss  = 0x23;
    proc->cpu_state.eflags = 0x202;

    process_count++;
    vga_printf("[scheduler] process '%s' created (pid=%d)\n", name, proc->pid);
}

void process_exit(int code) {
    if (current_process < 0) return;
    process_t* proc = &processes[current_process];
    proc->state = PROC_UNUSED;
    proc->exit_code = code;
    vga_printf("[scheduler] process '%s' exited (code=%d)\n", proc->name, code);
    process_count--;

    if (process_count == 0) {
        vga_puts("[scheduler] all processes exited, halting.\n");
        current_process = -1;
        cli();
        hlt();
    } else {
        current_process = -1;
    }
}

void scheduler_tick(void) {
    if (current_process >= 0 && processes[current_process].state == PROC_RUNNING) {
        processes[current_process].time_slices++;
    }

    int next = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        int idx = (current_process + 1 + i) % MAX_PROCESSES;
        if (processes[idx].state == PROC_READY || processes[idx].state == PROC_RUNNING) {
            next = idx;
            break;
        }
    }

    if (next < 0 || next == current_process) return;

    if (current_process >= 0 && processes[current_process].state == PROC_RUNNING) {
        processes[current_process].state = PROC_READY;
    }

    processes[next].state = PROC_RUNNING;
    current_process = next;
}

void process_yield(void) {
    if (current_process >= 0) {
        processes[current_process].state = PROC_READY;
    }
}

process_t* scheduler_current(void) {
    if (current_process < 0) return NULL;
    return &processes[current_process];
}

process_t* scheduler_get(pid_t pid) {
    if (pid < 1 || pid > MAX_PROCESSES) return NULL;
    if (processes[pid - 1].state == PROC_UNUSED) return NULL;
    return &processes[pid - 1];
}
