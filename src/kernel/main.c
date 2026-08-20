#include <bootinfo.h>
#include <con.h>

__attribute__((noreturn)) void _start(BootInfo *boot_info) {
  for (uint32_t i = 0; i < 256; i++) {
    boot_info->pml4[i] = 0;
  }

  __asm__ volatile(
    "mov %%cr3, %%rax\n\t"
    "mov %%rax, %%cr3"
    :
    :
    : "rax", "memory"
  );

  console_init(boot_info);
  console_set_color(0x00FFFFFF, 0x00000000);
  console_clear();

  kprintf("NovaOS Cros!\n");

  for (;;) {
    __asm__ volatile("hlt");
  }
}
