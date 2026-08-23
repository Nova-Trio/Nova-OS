#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
  const char* name;
  uint64_t addr;
} KernelSymbol;


#define EXPORT_SYMBOL(sym) __attribute__((used, section("kernel_syms"))) static const KernelSymbol __ksym_##sym = { #sym, (uint64_t)&sym }

void krsym_init(void);
uint64_t krsym_lookup(const char* name);
