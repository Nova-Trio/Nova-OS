#include <bootinfo.h>
#include <con.h>
#include <gdt.h>
#include <idt.h>
#include <pmm.h>
#include <vmm.h>
#include <heap.h>
#include <acpi.h>
#include <hpet.h>

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

  gdt_init();
  idt_init();
  pmm_init(boot_info);
  vmm_init(boot_info);
  heap_init();


  console_init(boot_info);
  console_set_color(0x00FFFFFF, 0x00000000);
  console_clear();

  kprintf("NovaOS Cros!\n");

  acpi_init(boot_info->rsdp);
  acpi_dump_tables();
  hpet_init();



  //*(volatile uint32_t *)0x0 = 0x12345678;

  for (;;) {
    __asm__ volatile("hlt");
  }
}
