#include <bootinfo.h>
#include <console.h>
#include <gdt.h>
#include <idt.h>
#include <pmm.h>
#include <vmm.h>
#include <heap.h>
#include <acpi.h>
#include <hpet.h>
#include <lapic.h>
#include <pcie.h>
#include <nvme.h>
#include <string.h>
#include <gpt.h>
#include <fat32.h>
#include <module.h>

extern void syscall_init(void);
extern void syscall_test(void);

static volatile uint64_t g_timer_ticks = 0;

static void timer_interrupt_handler(Registers *regs) {
  (void)regs;
  g_timer_ticks++;
  lapic_eoi();
}

static void print_dir_entry(const Fat32DirEntry *entry, void *context) {
  (void)context;
  if (entry->is_directory) {
    kprintf("[DIR]  %s\n", entry->name);
  } else {
    kprintf("[FILE] %s (%u bytes)\n", entry->name, entry->file_size);
  }
}


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
  lapic_init();

  idt_register_handler(LAPIC_VECTOR_TIMER, timer_interrupt_handler);
  lapic_timer_start(100, LAPIC_VECTOR_TIMER);

  __asm__ volatile("sti");

  pcie_init();
  pcie_dump_devices();

  nvme_init();
  fs_init();

  fs_list_dir("/EFI/novaos", print_dir_entry, NULL);

  module_init();

  void *nv_driver_data = NULL;
  size_t nv_driver_size = 0;
  if (fs_read_file("/nova/drivers/nvidia.elf", &nv_driver_data, &nv_driver_size) == 0 && nv_driver_data) {
    module_load("nvidia", nv_driver_data, nv_driver_size);
    kfree(nv_driver_data);
  } else {
    kprintf("Could not read /nova/drivers/nvidia.elf\n");
  }

  syscall_init();
  kprintf("syscall test\n");
  syscall_test();




  //*(volatile uint32_t *)0x0 = 0x12345678;

  for (;;) {
    __asm__ volatile("hlt");
  }
}
