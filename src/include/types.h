#ifndef DANYA_TYPES_H
#define DANYA_TYPES_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char      int8_t;
typedef signed short     int16_t;
typedef signed int       int32_t;
typedef signed long long int64_t;

typedef uint32_t size_t;
typedef int32_t  ssize_t;
typedef int32_t  pid_t;
typedef int32_t  errno_t;

#define NULL ((void*)0)

#define true  1
#define false 0
typedef uint8_t bool;

#define INLINE static inline
#define UNUSED(x) (void)(x)

#define PAGE_SIZE 4096
#define KERNEL_BASE 0xC0000000

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp;
    uint32_t ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
} __attribute__((packed)) stack_state_t;

#endif
