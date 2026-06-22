#ifndef DANYA_TMPFS_H
#define DANYA_TMPFS_H

#include "types.h"

#define TMPFS_MAX_FILES 32
#define TMPFS_NAME_LEN  64
#define TMPFS_DATA_SIZE 4096

typedef struct {
    char     name[TMPFS_NAME_LEN];
    uint32_t size;
    bool     used;
    uint8_t  data[TMPFS_DATA_SIZE];
} tmpfs_file_t;

void     tmpfs_init(void);
int32_t  tmpfs_create(const char* name);
int32_t  tmpfs_write(const char* name, const void* data, uint32_t size);
int32_t  tmpfs_read(const char* name, void* buf, uint32_t max_size);
int32_t  tmpfs_delete(const char* name);
void     tmpfs_list(void);
int32_t  tmpfs_exists(const char* name);

#endif
