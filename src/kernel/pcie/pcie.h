#pragma once
#include <stdint.h>
#include <stddef.h>

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
#define PCI_CAP_ID_MSI  0x05
#define PCI_CAP_ID_PCIE 0x10
#define PCI_CAP_ID_MSIX 0x11

#define PCI_MSI_CTRL_ENABLE (1 << 0)
#define PCI_MSI_CTRL_64BIT (1 << 7)
#define PCI_MSIX_CTRL_ENABLE (1 << 15)
#define PCI_MSIX_CTRL_MASK_ALL (1 << 14)
#define PCI_MSIX_CTRL_TABLE_SIZE 0x07FF
#define PCI_MSIX_ENTRY_MASKED (1 << 0)

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

void pcie_init(void);
void pcie_scan_all(void);
void pcie_dump_devices(void);

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
