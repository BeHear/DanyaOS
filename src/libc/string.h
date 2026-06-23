#ifndef DANYA_STRING_H
#define DANYA_STRING_H

#include "../include/types.h"

void* memset(void* dest, int c, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
int   memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
int   strcmp(const char* a, const char* b);
int   strcasecmp(const char* a, const char* b);
int   strncmp(const char* a, const char* b, size_t n);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
char* strcat(char* dest, const char* src);
char* itoa(int value, char* buf, int base);
char* uitoa(uint32_t value, char* buf, int base);
int   atoi(const char* s);

#endif
