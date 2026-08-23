#pragma once
#include <stdint.h>
#include <stddef.h>

#define MODULE_BASE_VADDR 0xFFFFFFFF90000000ULL

typedef int (*ModuleInitFunc)(void);
typedef void (*ModuleExitFunc)(void);

typedef struct ModuleSection {
  uint64_t virt_addr;
  size_t size;
  uint64_t flags;
} ModuleSection;

typedef struct Module {
  char *name;
  ModuleInitFunc init;
  ModuleExitFunc exit;
  ModuleSection *sections;
  size_t section_count;
  uint64_t base_addr;
  size_t page_count;
  struct Module *next;
} Module;

void module_init(void);
Module *module_load(const char *name, const void *elf_data, size_t elf_size);
int module_unload(Module *mod);
Module *module_find(const char *name);
