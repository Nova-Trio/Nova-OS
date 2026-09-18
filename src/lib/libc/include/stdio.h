#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#define EOF (-1)

int printf(const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int puts(const char *s);
int putchar(int c);
