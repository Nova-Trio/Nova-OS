#pragma once
#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096ULL
#define HHDM_BASE 0xFFFF800000000000ULL

#define VMM_FLAG_PRESENT (1ULL << 0)
#define VMM_FLAG_WRITABLE (1ULL << 1)
#define VMM_FLAG_USER (1ULL << 2)
#define VMM_FLAG_WRITE_THROUGH (1ULL << 3)
#define VMM_FLAG_NO_CACHE (1ULL << 4)
#define VMM_FLAG_HUGE (1ULL << 7)
#define VMM_FLAG_NO_EXECUTE (1ULL << 63)

#define PCIE_INVALID_VENDOR_ID 0xFFFF

#define PCI_REG_VENDOR_ID 0x00
#define PCI_REG_DEVICE_ID 0x02
#define PCI_REG_COMMAND 0x04
#define PCI_REG_STATUS 0x06
#define PCI_REG_REVISION_ID 0x08
#define PCI_REG_PROG_IF 0x09
#define PCI_REG_SUBCLASS 0x0A
#define PCI_REG_CLASS_CODE 0x0B
#define PCI_REG_CACHE_LINE_SIZE 0x0C
#define PCI_REG_LATENCY_TIMER 0x0D
#define PCI_REG_HEADER_TYPE 0x0E
#define PCI_REG_BIST 0x0F
#define PCI_REG_BAR0 0x10
#define PCI_REG_BAR1 0x14
#define PCI_REG_BAR2 0x18
#define PCI_REG_BAR3 0x1C
#define PCI_REG_BAR4 0x20
#define PCI_REG_BAR5 0x24
#define PCI_REG_CAPABILITIES 0x34
#define PCI_REG_INTERRUPT_LINE 0x3C
#define PCI_REG_INTERRUPT_PIN 0x3D

#define PCI_CMD_IO_ENABLE (1 << 0)
#define PCI_CMD_MEM_ENABLE (1 << 1)
#define PCI_CMD_BUS_MASTER (1 << 2)

#define PCI_STATUS_CAPABILITIES (1 << 4)

#define PCI_HEADER_TYPE_MASK 0x7F
#define PCI_HEADER_TYPE_NORMAL 0x00
#define PCI_HEADER_TYPE_BRIDGE 0x01
#define PCI_HEADER_TYPE_CARDBUS 0x02
#define PCI_HEADER_TYPE_MULTIFUNC 0x80

#define PCI_BAR_TYPE_IO (1 << 0)
#define PCI_BAR_MEM_TYPE_MASK (3 << 1)
#define PCI_BAR_MEM_TYPE_32 (0 << 1)
#define PCI_BAR_MEM_TYPE_64 (2 << 1)
#define PCI_BAR_MEM_PREFETCH (1 << 3)

#define PCI_CAP_ID_PM 0x01
#define PCI_CAP_ID_MSI 0x05
#define PCI_CAP_ID_PCIE 0x10
#define PCI_CAP_ID_MSIX 0x11

#define PCI_MSI_CTRL_ENABLE (1 << 0)
#define PCI_MSI_CTRL_64BIT (1 << 7)
#define PCI_MSIX_CTRL_ENABLE (1 << 15)
#define PCI_MSIX_CTRL_MASK_ALL (1 << 14)
#define PCI_MSIX_CTRL_TABLE_SIZE 0x07FF
#define PCI_MSIX_ENTRY_MASKED (1 << 0)

#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN 0x02
#define FAT_ATTR_SYSTEM 0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE 0x20


typedef struct {
  uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
  uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
  uint64_t vector;
  uint64_t error_code;
  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
  uint64_t rsp;
  uint64_t ss;
} __attribute__((packed)) Registers;

typedef struct {
  uint32_t msg_addr_low;
  uint32_t msg_addr_high;
  uint32_t msg_data;
  uint32_t vector_ctrl;
} __attribute__((packed)) PciMsixTableEntry;

typedef struct {
  uint64_t phys_addr;
  uint64_t size;
  uint8_t is_io;
  uint8_t is_64bit;
  uint8_t is_prefetchable;
} PciBar;

typedef struct {
  uint16_t segment;
  uint8_t bus;
  uint8_t device;
  uint8_t function;

  uint16_t vendor_id;
  uint16_t device_id;
  uint8_t revision_id;
  uint8_t prog_if;
  uint8_t subclass;
  uint8_t class_code;
  uint8_t header_type;

  uint16_t subsystem_vendor_id;
  uint16_t subsystem_id;

  uint8_t interrupt_line;
  uint8_t interrupt_pin;

  PciBar bars[6];
  uint8_t bar_count;
} PciDevice;

typedef struct {
  char     name[256];
  uint32_t start_cluster;
  uint32_t file_size;
  uint8_t  attributes;
  uint8_t  is_directory;
} Fat32DirEntry;

typedef uint64_t *PageDirectory;
typedef void (*InterruptHandler)(Registers *regs);
typedef void (*Fat32DirCallback)(const Fat32DirEntry *entry, void *context);

int kprintf(const char *fmt, ...);

void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *krealloc(void *ptr, size_t new_size);
void kfree(void *ptr);

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
int strcmp(const char *s1, const char *s2);
size_t strlen(const char *str);

int vmm_map_page(PageDirectory pml4, uint64_t virt, uint64_t phys, uint64_t flags);
int vmm_unmap_page(PageDirectory pml4, uint64_t virt);
int vmm_map_range(PageDirectory pml4, uint64_t virt_start, uint64_t phys_start, uint64_t size, uint64_t flags);
int vmm_unmap_range(PageDirectory pml4, uint64_t virt_start, uint64_t size);
uint64_t vmm_virt_to_phys(PageDirectory pml4, uint64_t virt);
PageDirectory vmm_get_kernel_pml4(void);

void *pmm_alloc_frame(void);
void *pmm_alloc_frames(size_t count);
void pmm_free_frame(void *phys_addr);
void pmm_free_frames(void *phys_addr, size_t count);

void hpet_sleep_ms(uint64_t ms);
void hpet_sleep_us(uint64_t us);
uint64_t hpet_get_nanos(void);
uint64_t hpet_get_millis(void);

uint32_t lapic_get_id(void);
void lapic_eoi(void);
void idt_register_handler(uint8_t vector, InterruptHandler handler);
void idt_unregister_handler(uint8_t vector);

size_t pcie_get_device_count(void);
const PciDevice *pcie_get_device(size_t index);
const PciDevice *pcie_find_device(uint16_t vendor_id, uint16_t device_id);
const PciDevice *pcie_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if);

uint8_t pcie_read8(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);
uint16_t pcie_read16(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);
uint32_t pcie_read32(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);

void pcie_write8(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint8_t value);
void pcie_write16(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint16_t value);
void pcie_write32(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint32_t value);

uint8_t pcie_find_capability(const PciDevice *dev, uint8_t cap_id);
uint16_t pcie_find_extended_capability(const PciDevice *dev, uint16_t cap_id);
void pcie_enable_bus_master(const PciDevice *dev);

int pcie_enable_msi(const PciDevice *dev, uint8_t vector, uint32_t lapic_id);
int pcie_disable_msi(const PciDevice *dev);
uint16_t pcie_get_msix_table_size(const PciDevice *dev);
int pcie_enable_msix_vector(const PciDevice *dev, uint16_t table_index, uint8_t vector, uint32_t lapic_id);
int pcie_mask_msix_vector(const PciDevice *dev, uint16_t table_index, int masked);
int pcie_enable_msix(const PciDevice *dev);
int pcie_disable_msix(const PciDevice *dev);

int fs_read_file(const char *path, void **out_buf, size_t *out_size);
int fs_stat(const char *path, Fat32DirEntry *out_entry);
int fs_read(const char *path, uint64_t offset, size_t size, void *buf, size_t *bytes_read);
int fs_list_dir(const char *path, Fat32DirCallback callback, void *context);

int driver_init(void);
void driver_exit(void);
