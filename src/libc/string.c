#include "string.h"

/*
 * String operations — original C implementations.
 * Kept in C because the Rust compiler_builtins provides conflicting
 * memset/memcpy symbols that cause infinite recursion when called
 * via the Rust FFI bridge with --whole-archive.
 */

void* memset(void* dest, int c, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    while (n--) *d++ = (uint8_t)c;
    return dest;
}

void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dest;
}

int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    while (n--) {
        if (*pa != *pb) return *pa - *pb;
        pa++;
        pb++;
    }
    return 0;
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *(const uint8_t*)a - *(const uint8_t*)b;
}

int strcasecmp(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return (uint8_t)ca - (uint8_t)cb;
        a++; b++;
    }
    return (uint8_t)(*a) - (uint8_t)(*b);
}

int strncmp(const char* a, const char* b, size_t n) {
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0) return 0;
    return *(const uint8_t*)a - *(const uint8_t*)b;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    char* d = dest;
    while (n && (*d++ = *src++)) n--;
    while (n--) *d++ = '\0';
    return dest;
}

char* strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

char* itoa(int value, char* buf, int base) {
    char* ptr = buf;
    char* ptr1 = buf;
    char tmp;
    int negative = 0;

    if (value == 0) {
        *ptr++ = '0';
        *ptr = '\0';
        return buf;
    }

    uint32_t uv;
    if (value < 0 && base == 10) {
        negative = 1;
        uv = -(uint32_t)value;
    } else {
        uv = (uint32_t)value;
    }

    while (uv) {
        int rem = uv % base;
        *ptr++ = (rem > 9) ? ('a' + rem - 10) : ('0' + rem);
        uv /= base;
    }

    if (negative) *ptr++ = '-';
    *ptr = '\0';

    ptr1 = buf;
    ptr--;
    while (ptr1 < ptr) {
        tmp = *ptr1;
        *ptr1 = *ptr;
        *ptr = tmp;
        ptr1++;
        ptr--;
    }

    return buf;
}

char* uitoa(uint32_t value, char* buf, int base) {
    char* ptr = buf;
    char* ptr1 = buf;
    char tmp;

    if (value == 0) {
        *ptr++ = '0';
        *ptr = '\0';
        return buf;
    }

    while (value) {
        int rem = value % base;
        *ptr++ = (rem > 9) ? ('a' + rem - 10) : ('0' + rem);
        value /= base;
    }

    *ptr = '\0';

    ptr1 = buf;
    ptr--;
    while (ptr1 < ptr) {
        tmp = *ptr1;
        *ptr1 = *ptr;
        *ptr = tmp;
        ptr1++;
        ptr--;
    }

    return buf;
}

int atoi(const char* s) {
    int res = 0;
    int sign = 1;

    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;

    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }

    return res * sign;
}
