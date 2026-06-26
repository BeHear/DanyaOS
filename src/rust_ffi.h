#ifndef DANYA_RUST_FFI_H
#define DANYA_RUST_FFI_H

#include "../include/types.h"

/* ===== PMM (Physical Memory Manager) ===== */
void     rust_pmm_init(uint32_t total_memory);
void*    rust_pmm_alloc_page(void);
void     rust_pmm_free_page(void* page);
uint32_t rust_pmm_get_free_count(void);
uint32_t rust_pmm_get_total_count(void);

/* ===== IPC ===== */
void     rust_ipc_init(void);
int32_t  rust_ipc_send(int32_t from, int32_t to, const char* data, uint32_t length);
int32_t  rust_ipc_receive(int32_t from, char* buf, uint32_t max_len);

#endif
