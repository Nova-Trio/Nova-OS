#include <bootinfo.h>

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

  uint32_t *fb = (uint32_t *)boot_info->framebuffer.base;
  uint32_t pitch = boot_info->framebuffer.pixels_per_scanline;

  for (uint32_t y = 0; y < boot_info->framebuffer.height; y++) {
    for (uint32_t x = 0; x < boot_info->framebuffer.width; x++) {
      fb[y * pitch + x] = 0x0000FF00;
    }
  }


  for (;;) {
    __asm__ volatile("hlt");
  }
}
