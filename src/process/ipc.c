#include "ipc.h"
#include "scheduler.h"
#include "../memory/heap.h"
#include "../libc/string.h"
#include "../drivers/vga.h"
#include "../rust_ffi.h"

/*
 * IPC — C wrapper that delegates to the Rust implementation.
 * The Rust code in rust/src/ipc.rs provides a lock-free circular
 * message queue with atomic operations and bounds-checked copies.
 */

void ipc_init(void) {
    rust_ipc_init();
}

int32_t ipc_send(pid_t to, const char* data, uint32_t length) {
    process_t* receiver = scheduler_get(to);
    if (!receiver) {
        vga_printf("[ipc] pid %d not found\n", to);
        return -1;
    }
    process_t* sender = scheduler_current();
    pid_t from = sender ? sender->pid : 0;
    return rust_ipc_send(from, to, data, length);
}

int32_t ipc_receive(pid_t from, char* buf, uint32_t max_len) {
    return rust_ipc_receive(from, buf, max_len);
}
