#pragma once
#include <stddef.h>
#include <stdint.h>

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

__attribute__((noreturn)) void exit(int status);
__attribute__((noreturn)) void abort(void);

int atexit(void (*func)(void));
int __cxa_atexit(void (*func)(void *), void *arg, void *dso);

int atoi(const char *nptr);
int abs(int j);
