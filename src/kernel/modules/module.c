#include "module.h"
#include <elf.h>
#include <krsym.h>
#include <vmm.h>
#include <pmm.h>
#include <heap.h>
#include <console.h>
#include <string.h>

typedef struct ModuleFreeNode {
  uint64_t virt_addr;
  size_t page_count;
  struct ModuleFreeNode *next;
} ModuleFreeNode;

static uint64_t g_module_virt_cursor = MODULE_BASE_VADDR;
static ModuleFreeNode *g_module_free_ranges = NULL;
static Module *g_loaded_modules = NULL;

void module_init(void) {
  g_module_virt_cursor = MODULE_BASE_VADDR;
  g_module_free_ranges = NULL;
  g_loaded_modules = NULL;
  krsym_init();
}

static void recycle_module_range(uint64_t virt_addr, size_t page_count) {
  ModuleFreeNode *node = (ModuleFreeNode *)kmalloc(sizeof(ModuleFreeNode));
  if (!node) {
    return;
  }

  node->virt_addr = virt_addr;
  node->page_count = page_count;
  node->next = NULL;

  ModuleFreeNode **curr = &g_module_free_ranges;
  while (*curr && (*curr)->virt_addr < virt_addr) {
    curr = &(*curr)->next;
  }

  node->next = *curr;
  *curr = node;

  ModuleFreeNode *it = g_module_free_ranges;
  while (it && it->next) {
    if (it->virt_addr + (it->page_count * PAGE_SIZE) == it->next->virt_addr) {
      ModuleFreeNode *merged = it->next;
      it->page_count += merged->page_count;
      it->next = merged->next;
      kfree(merged);
    } else {
      it = it->next;
    }
  }
}

static uint64_t alloc_module_pages(size_t page_count) {
  if (page_count == 0) {
    return 0;
  }

  PageDirectory kernel_pml4 = vmm_get_kernel_pml4();
  uint64_t virt_base = 0;

  ModuleFreeNode **curr = &g_module_free_ranges;
  while (*curr) {
    if ((*curr)->page_count == page_count) {
      ModuleFreeNode *node = *curr;
      virt_base = node->virt_addr;
      *curr = node->next;
      kfree(node);
      break;
    } else if ((*curr)->page_count > page_count) {
      ModuleFreeNode *node = *curr;
      virt_base = node->virt_addr;
      node->virt_addr += page_count * PAGE_SIZE;
      node->page_count -= page_count;
      break;
    }
    curr = &(*curr)->next;
  }

  if (!virt_base) {
    virt_base = g_module_virt_cursor;
    g_module_virt_cursor += page_count * PAGE_SIZE;
  }

  for (size_t i = 0; i < page_count; i++) {
    void *frame = pmm_alloc_frame();
    if (!frame) {
      for (size_t j = 0; j < i; j++) {
        uint64_t v = virt_base + (j * PAGE_SIZE);
        uint64_t ph = vmm_virt_to_phys(kernel_pml4, v);
        vmm_unmap_page(kernel_pml4, v);
        if (ph) pmm_free_frame((void *)ph);
      }
      recycle_module_range(virt_base, page_count);
      return 0;
    }

    uint64_t v = virt_base + (i * PAGE_SIZE);
    if (vmm_map_page(kernel_pml4, v, (uint64_t)frame, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE) != 0) {
      pmm_free_frame(frame);
      for (size_t j = 0; j < i; j++) {
        uint64_t v_prev = virt_base + (j * PAGE_SIZE);
        uint64_t ph = vmm_virt_to_phys(kernel_pml4, v_prev);
        vmm_unmap_page(kernel_pml4, v_prev);
        if (ph) pmm_free_frame((void *)ph);
      }
      recycle_module_range(virt_base, page_count);
      return 0;
    }
  }

  return virt_base;
}

static void free_module_pages(uint64_t virt_base, size_t page_count) {
  if (!virt_base || page_count == 0) {
    return;
  }

  PageDirectory kernel_pml4 = vmm_get_kernel_pml4();
  for (size_t i = 0; i < page_count; i++) {
    uint64_t v = virt_base + (i * PAGE_SIZE);
    uint64_t ph = vmm_virt_to_phys(kernel_pml4, v);
    vmm_unmap_page(kernel_pml4, v);
    if (ph) {
      pmm_free_frame((void *)ph);
    }
  }

  recycle_module_range(virt_base, page_count);
}

static int apply_relocations(const uint8_t *raw_elf, const Elf64_Shdr *shdrs, size_t shnum, size_t rel_idx, const ModuleSection *loaded_secs, const Elf64_Sym *symtab, size_t sym_count, const char *strtab) {
  const Elf64_Shdr *rel_shdr = &shdrs[rel_idx];
  const ModuleSection *target_sec = &loaded_secs[rel_shdr->sh_info];

  if (!target_sec->virt_addr) {
    return 0;
  }

  const Elf64_Rela *relas = (const Elf64_Rela *)(raw_elf + rel_shdr->sh_offset);
  size_t count = rel_shdr->sh_size / sizeof(Elf64_Rela);

  for (size_t i = 0; i < count; i++) {
    const Elf64_Rela *rel = &relas[i];
    uint32_t sym_idx = (uint32_t)ELF64_R_SYM(rel->r_info);
    uint32_t rel_type = (uint32_t)ELF64_R_TYPE(rel->r_info);

    if (sym_idx >= sym_count) {
      kprintf("[MODULE] Error: Relocation symbol index out of bounds: %u\n", sym_idx);
      return -1;
    }

    const Elf64_Sym *sym = &symtab[sym_idx];
    uint64_t s = 0;

    if (sym->st_shndx == SHN_UNDEF) {
      const char *sym_name = strtab + sym->st_name;
      s = krsym_lookup(sym_name);
      if (!s) {
        kprintf("[MODULE] Error: Unresolved symbol: '%s'\n", sym_name);
        return -1;
      }
    } else if (sym->st_shndx == SHN_ABS) {
      s = sym->st_value;
    } else if (sym->st_shndx < shnum) {
      s = loaded_secs[sym->st_shndx].virt_addr + sym->st_value;
    } else {
      kprintf("[MODULE] Error: Unsupported symbol section index: %u\n", (uint32_t)sym->st_shndx);
      return -1;
    }

    uint64_t p = target_sec->virt_addr + rel->r_offset;
    int64_t a = rel->r_addend;

    switch (rel_type) {
      case R_X86_64_NONE:
        break;

      case R_X86_64_64: {
        *(uint64_t *)p = (uint64_t)(s + a);
        break;
      }

      case R_X86_64_32: {
        uint64_t val = s + a;
        if (val > 0xFFFFFFFFULL) {
          kprintf("[MODULE] Error: Relocation R_X86_64_32 overflow (0x%016llx)\n", val);
          return -1;
        }
        *(uint32_t *)p = (uint32_t)val;
        break;
      }

      case R_X86_64_32S: {
        int64_t val = (int64_t)(s + a);
        if (val < -0x80000000LL || val > 0x7FFFFFFFLL) {
          kprintf("[MODULE] Error: Relocation R_X86_64_32S overflow (%lld)\n", val);
          return -1;
        }
        *(int32_t *)p = (int32_t)val;
        break;
      }

      case R_X86_64_PC32:
      case R_X86_64_PLT32: {
        int64_t val = (int64_t)(s + a - p);
        if (val < -0x80000000LL || val > 0x7FFFFFFFLL) {
          kprintf("[MODULE] Error: Relocation PC32 overflow (%lld) between 0x%016llx and 0x%016llx\n",
                  val, s, p);
          return -1;
        }
        *(int32_t *)p = (int32_t)val;
        break;
      }

      default:
        kprintf("[MODULE] Error: Unsupported relocation type: %u\n", rel_type);
        return -1;
    }
  }

  return 0;
}

Module *module_load(const char *name, const void *elf_data, size_t elf_size) {
  if (!name || !elf_data || elf_size < sizeof(Elf64_Ehdr)) {
    return NULL;
  }

  const uint8_t *raw = (const uint8_t *)elf_data;
  const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)raw;

  if (ehdr->e_ident[0] != ELFMAG0 || ehdr->e_ident[1] != ELFMAG1 ||
    ehdr->e_ident[2] != ELFMAG2 || ehdr->e_ident[3] != ELFMAG3 ||
    ehdr->e_ident[4] != ELFCLASS64 || ehdr->e_ident[5] != ELFDATA2LSB ||
    ehdr->e_machine != EM_X86_64 || ehdr->e_type != ET_REL) {
    kprintf("[MODULE] Error: '%s' is not a valid x86_64 ET_REL ELF\n", name);
  return NULL;
    }

    if (ehdr->e_shoff + ((uint64_t)ehdr->e_shnum * ehdr->e_shentsize) > elf_size) {
      kprintf("[MODULE] Error: Corrupted section headers in '%s'\n", name);
      return NULL;
    }

    const Elf64_Shdr *shdrs = (const Elf64_Shdr *)(raw + ehdr->e_shoff);

    size_t total_alloc_size = 0;
    for (size_t i = 0; i < ehdr->e_shnum; i++) {
      if (shdrs[i].sh_flags & SHF_ALLOC) {
        uint64_t align = shdrs[i].sh_addralign ? shdrs[i].sh_addralign : 1;
        total_alloc_size = (total_alloc_size + align - 1) & ~(align - 1);
        total_alloc_size += shdrs[i].sh_size;
      }
    }

    if (total_alloc_size == 0) {
      kprintf("[MODULE] Error: No allocatable sections in '%s'\n", name);
      return NULL;
    }

    size_t page_count = (total_alloc_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t base_vaddr = alloc_module_pages(page_count);
    if (!base_vaddr) {
      kprintf("[MODULE] Error: Out of virtual memory for module '%s'\n", name);
      return NULL;
    }

    ModuleSection *loaded_secs = (ModuleSection *)kzalloc(ehdr->e_shnum * sizeof(ModuleSection));
    if (!loaded_secs) {
      free_module_pages(base_vaddr, page_count);
      return NULL;
    }

    uint64_t current_offset = 0;
    for (size_t i = 0; i < ehdr->e_shnum; i++) {
      if (shdrs[i].sh_flags & SHF_ALLOC) {
        uint64_t align = shdrs[i].sh_addralign ? shdrs[i].sh_addralign : 1;
        current_offset = (current_offset + align - 1) & ~(align - 1);

        uint64_t sec_vaddr = base_vaddr + current_offset;
        loaded_secs[i].virt_addr = sec_vaddr;
        loaded_secs[i].size = shdrs[i].sh_size;
        loaded_secs[i].flags = shdrs[i].sh_flags;

        if (shdrs[i].sh_type == SHT_NOBITS) {
          memset((void *)sec_vaddr, 0, shdrs[i].sh_size);
        } else {
          memcpy((void *)sec_vaddr, raw + shdrs[i].sh_offset, shdrs[i].sh_size);
        }

        current_offset += shdrs[i].sh_size;
      }
    }

    const Elf64_Sym *symtab = NULL;
    size_t sym_count = 0;
    const char *strtab = NULL;

    for (size_t i = 0; i < ehdr->e_shnum; i++) {
      if (shdrs[i].sh_type == SHT_SYMTAB) {
        symtab = (const Elf64_Sym *)(raw + shdrs[i].sh_offset);
        sym_count = shdrs[i].sh_size / sizeof(Elf64_Sym);
        strtab = (const char *)(raw + shdrs[shdrs[i].sh_link].sh_offset);
        break;
      }
    }

    if (!symtab || !strtab) {
      kprintf("[MODULE] Error: Missing symbol table in '%s'\n", name);
      free_module_pages(base_vaddr, page_count);
      kfree(loaded_secs);
      return NULL;
    }

    for (size_t i = 0; i < ehdr->e_shnum; i++) {
      if (shdrs[i].sh_type == SHT_RELA) {
        if (apply_relocations(raw, shdrs, ehdr->e_shnum, i, loaded_secs, symtab, sym_count, strtab) != 0) {
          kprintf("[MODULE] Error: Failed to apply relocations for '%s'\n", name);
          free_module_pages(base_vaddr, page_count);
          kfree(loaded_secs);
          return NULL;
        }
      }
    }

    ModuleInitFunc init_fn = NULL;
    ModuleExitFunc exit_fn = NULL;

    for (size_t i = 0; i < sym_count; i++) {
      const char *sname = strtab + symtab[i].st_name;
      if (strcmp(sname, "driver_init") == 0) {
        init_fn = (ModuleInitFunc)(loaded_secs[symtab[i].st_shndx].virt_addr + symtab[i].st_value);
      } else if (strcmp(sname, "driver_exit") == 0) {
        exit_fn = (ModuleExitFunc)(loaded_secs[symtab[i].st_shndx].virt_addr + symtab[i].st_value);
      }
    }

    if (!init_fn) {
      kprintf("[MODULE] Error: 'driver_init' not found in '%s'\n", name);
      free_module_pages(base_vaddr, page_count);
      kfree(loaded_secs);
      return NULL;
    }

    size_t name_len = strlen(name);
    char *name_copy = (char *)kmalloc(name_len + 1);
    if (!name_copy) {
      free_module_pages(base_vaddr, page_count);
      kfree(loaded_secs);
      return NULL;
    }
    memcpy(name_copy, name, name_len + 1);

    Module *mod = (Module *)kzalloc(sizeof(Module));
    if (!mod) {
      kfree(name_copy);
      free_module_pages(base_vaddr, page_count);
      kfree(loaded_secs);
      return NULL;
    }

    mod->name = name_copy;
    mod->init = init_fn;
    mod->exit = exit_fn;
    mod->sections = loaded_secs;
    mod->section_count = ehdr->e_shnum;
    mod->base_addr = base_vaddr;
    mod->page_count = page_count;

    int res = mod->init();
    if (res != 0) {
      kprintf("[MODULE] Error: driver_init for '%s' returned %d\n", name, res);
      kfree(mod->name);
      free_module_pages(base_vaddr, page_count);
      kfree(loaded_secs);
      kfree(mod);
      return NULL;
    }

    mod->next = g_loaded_modules;
    g_loaded_modules = mod;

    kprintf("[MODULE] Loaded '%s' at 0x%016llx\n", name, base_vaddr);

    return mod;
}

int module_unload(Module *mod) {
  if (!mod) {
    return -1;
  }

  Module **curr = &g_loaded_modules;
  while (*curr && *curr != mod) {
    curr = &(*curr)->next;
  }

  if (!*curr) {
    kprintf("[MODULE] Error: Module '%s' not registered\n", mod->name);
    return -1;
  }

  *curr = mod->next;

  if (mod->exit) {
    mod->exit();
  }

  free_module_pages(mod->base_addr, mod->page_count);
  kfree(mod->sections);
  kprintf("[MODULE] Unloaded '%s'\n", mod->name);
  kfree(mod->name);
  kfree(mod);

  return 0;
}

Module *module_find(const char *name) {
  if (!name) {
    return NULL;
  }

  Module *curr = g_loaded_modules;
  while (curr) {
    if (strcmp(curr->name, name) == 0) {
      return curr;
    }
    curr = curr->next;
  }

  return NULL;
}
