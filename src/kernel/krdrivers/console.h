#pragma once
#include <bootinfo.h>
#include <stdint.h>
#include <stdarg.h>

void console_init(BootInfo *boot_info);
void console_set_color(uint32_t fg, uint32_t bg);
void console_clear(void);
void console_putchar(char c);

int kvprintf(const char *fmt, va_list args);
int kprintf(const char *fmt, ...);
