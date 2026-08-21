#include "pcie.h"
#include <acpi.h>
#include <vmm.h>
#include <heap.h>
#include <console.h>
#include <string.h>

static AcpiMcfgAllocation *g_segments = NULL;
static size_t g_segment_count = 0;

static PciDevice *g_devices = NULL;
static size_t g_device_count = 0;
static size_t g_device_capacity = 0;

static volatile void *pcie_get_reg_addr(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset) {
  if (device >= 32 || function >= 8 || offset >= 4096) {
    return NULL;
  }

  const AcpiMcfgAllocation *alloc = NULL;
  for (size_t i = 0; i < g_segment_count; i++) {
    if (g_segments[i].segment_group == segment &&
      bus >= g_segments[i].start_bus &&
      bus <= g_segments[i].end_bus) {
      alloc = &g_segments[i];
    break;
      }
  }

  if (!alloc) {
    return NULL;
  }

  uint64_t bus_offset = (uint64_t)(bus - alloc->start_bus) << 20;
  uint64_t dev_offset = (uint64_t)device << 15;
  uint64_t func_offset = (uint64_t)function << 12;
  uint64_t page_phys = alloc->base_address + bus_offset + dev_offset + func_offset;
  uint64_t page_virt = page_phys + HHDM_BASE;

  PageDirectory pml4 = vmm_get_kernel_pml4();
  if (!vmm_virt_to_phys(pml4, page_virt)) {
    if (vmm_map_page(pml4, page_virt, page_phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NO_CACHE) != 0) {
      return NULL;
    }
  }

  return (volatile void *)(page_virt + offset);
}

uint8_t pcie_read8(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset) {
  volatile uint8_t *addr = (volatile uint8_t *)pcie_get_reg_addr(segment, bus, device, function, offset);
  if (!addr) {
    return 0xFF;
  }
  return *addr;
}

uint16_t pcie_read16(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset) {
  if (offset & 1) {
    return 0xFFFF;
  }
  volatile uint16_t *addr = (volatile uint16_t *)pcie_get_reg_addr(segment, bus, device, function, offset);
  if (!addr) {
    return 0xFFFF;
  }
  return *addr;
}

uint32_t pcie_read32(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset) {
  if (offset & 3) {
    return 0xFFFFFFFF;
  }
  volatile uint32_t *addr = (volatile uint32_t *)pcie_get_reg_addr(segment, bus, device, function, offset);
  if (!addr) {
    return 0xFFFFFFFF;
  }
  return *addr;
}

void pcie_write8(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint8_t value) {
  volatile uint8_t *addr = (volatile uint8_t *)pcie_get_reg_addr(segment, bus, device, function, offset);
  if (addr) {
    *addr = value;
  }
}

void pcie_write16(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint16_t value) {
  if (offset & 1) {
    return;
  }
  volatile uint16_t *addr = (volatile uint16_t *)pcie_get_reg_addr(segment, bus, device, function, offset);
  if (addr) {
    *addr = value;
  }
}

void pcie_write32(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint32_t value) {
  if (offset & 3) {
    return;
  }
  volatile uint32_t *addr = (volatile uint32_t *)pcie_get_reg_addr(segment, bus, device, function, offset);
  if (addr) {
    *addr = value;
  }
}

static void pcie_parse_bars(PciDevice *dev) {
  uint8_t max_bars = ((dev->header_type & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_BRIDGE) ? 2 : 6;
  dev->bar_count = max_bars;

  uint16_t orig_cmd = pcie_read16(dev->segment, dev->bus, dev->device, dev->function, PCI_REG_COMMAND);
  pcie_write16(dev->segment, dev->bus, dev->device, dev->function, PCI_REG_COMMAND, orig_cmd & ~(PCI_CMD_IO_ENABLE | PCI_CMD_MEM_ENABLE));

  for (uint8_t i = 0; i < max_bars; i++) {
    uint16_t bar_offset = PCI_REG_BAR0 + (i * 4);
    uint32_t bar_low = pcie_read32(dev->segment, dev->bus, dev->device, dev->function, bar_offset);

    if (bar_low == 0 || bar_low == 0xFFFFFFFF) {
      dev->bars[i].phys_addr = 0;
      dev->bars[i].size = 0;
      continue;
    }

    if (bar_low & PCI_BAR_TYPE_IO) {
      dev->bars[i].is_io = 1;
      dev->bars[i].is_64bit = 0;
      dev->bars[i].is_prefetchable = 0;

      pcie_write32(dev->segment, dev->bus, dev->device, dev->function, bar_offset, 0xFFFFFFFF);
      uint32_t mask = pcie_read32(dev->segment, dev->bus, dev->device, dev->function, bar_offset);
      pcie_write32(dev->segment, dev->bus, dev->device, dev->function, bar_offset, bar_low);

      uint32_t size = ~(mask & ~0x3) + 1;
      dev->bars[i].phys_addr = bar_low & ~0x3;
      dev->bars[i].size = size;
    } else {
      dev->bars[i].is_io = 0;
      dev->bars[i].is_prefetchable = (bar_low & PCI_BAR_MEM_PREFETCH) ? 1 : 0;
      uint8_t mem_type = bar_low & PCI_BAR_MEM_TYPE_MASK;

      if (mem_type == PCI_BAR_MEM_TYPE_64 && (i + 1) < max_bars) {
        dev->bars[i].is_64bit = 1;
        uint16_t bar_high_offset = PCI_REG_BAR0 + ((i + 1) * 4);
        uint32_t bar_high = pcie_read32(dev->segment, dev->bus, dev->device, dev->function, bar_high_offset);

        pcie_write32(dev->segment, dev->bus, dev->device, dev->function, bar_offset, 0xFFFFFFFF);
        pcie_write32(dev->segment, dev->bus, dev->device, dev->function, bar_high_offset, 0xFFFFFFFF);

        uint32_t mask_low = pcie_read32(dev->segment, dev->bus, dev->device, dev->function, bar_offset);
        uint32_t mask_high = pcie_read32(dev->segment, dev->bus, dev->device, dev->function, bar_high_offset);

        pcie_write32(dev->segment, dev->bus, dev->device, dev->function, bar_offset, bar_low);
        pcie_write32(dev->segment, dev->bus, dev->device, dev->function, bar_high_offset, bar_high);

        uint64_t full_mask = ((uint64_t)mask_high << 32) | (mask_low & ~0xF);
        uint64_t size = ~full_mask + 1;

        dev->bars[i].phys_addr = (((uint64_t)bar_high << 32) | (bar_low & ~0xF));
        dev->bars[i].size = size;

        i++;
        dev->bars[i].phys_addr = 0;
        dev->bars[i].size = 0;
        dev->bars[i].is_64bit = 1;
      } else {
        dev->bars[i].is_64bit = 0;

        pcie_write32(dev->segment, dev->bus, dev->device, dev->function, bar_offset, 0xFFFFFFFF);
        uint32_t mask = pcie_read32(dev->segment, dev->bus, dev->device, dev->function, bar_offset);
        pcie_write32(dev->segment, dev->bus, dev->device, dev->function, bar_offset, bar_low);

        uint32_t size = ~(mask & ~0xF) + 1;
        dev->bars[i].phys_addr = bar_low & ~0xF;
        dev->bars[i].size = size;
      }
    }
  }

  pcie_write16(dev->segment, dev->bus, dev->device, dev->function, PCI_REG_COMMAND, orig_cmd);
}


static void pcie_register_device(PciDevice *dev) {
  if (g_device_count >= g_device_capacity) {
    size_t new_cap = (g_device_capacity == 0) ? 16 : g_device_capacity * 2;
    PciDevice *new_arr = (PciDevice *)krealloc(g_devices, new_cap * sizeof(PciDevice));
    if (!new_arr) {
      kprintf("[PCIe] Error: Failed to grow device table\n");
      return;
    }
    g_devices = new_arr;
    g_device_capacity = new_cap;
  }

  g_devices[g_device_count++] = *dev;
}

static void pcie_probe_function(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function) {
  uint16_t vendor_id = pcie_read16(segment, bus, device, function, PCI_REG_VENDOR_ID);
  if (vendor_id == PCIE_INVALID_VENDOR_ID) {
    return;
  }

  PciDevice dev;
  dev.segment = segment;
  dev.bus = bus;
  dev.device = device;
  dev.function = function;
  dev.vendor_id = vendor_id;
  dev.device_id = pcie_read16(segment, bus, device, function, PCI_REG_DEVICE_ID);
  dev.revision_id = pcie_read8(segment, bus, device, function, PCI_REG_REVISION_ID);
  dev.prog_if = pcie_read8(segment, bus, device, function, PCI_REG_PROG_IF);
  dev.subclass = pcie_read8(segment, bus, device, function, PCI_REG_SUBCLASS);
  dev.class_code = pcie_read8(segment, bus, device, function, PCI_REG_CLASS_CODE);
  dev.header_type = pcie_read8(segment, bus, device, function, PCI_REG_HEADER_TYPE);
  dev.interrupt_line = pcie_read8(segment, bus, device, function, PCI_REG_INTERRUPT_LINE);
  dev.interrupt_pin = pcie_read8(segment, bus, device, function, PCI_REG_INTERRUPT_PIN);

  if ((dev.header_type & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_NORMAL) {
    dev.subsystem_vendor_id = pcie_read16(segment, bus, device, function, 0x2C);
    dev.subsystem_id = pcie_read16(segment, bus, device, function, 0x2E);
  } else {
    dev.subsystem_vendor_id = 0;
    dev.subsystem_id = 0;
  }

  pcie_parse_bars(&dev);
  pcie_register_device(&dev);
}

static void pcie_probe_device(uint16_t segment, uint8_t bus, uint8_t device) {
  uint16_t vendor_id = pcie_read16(segment, bus, device, 0, PCI_REG_VENDOR_ID);
  if (vendor_id == PCIE_INVALID_VENDOR_ID) {
    return;
  }

  uint8_t header_type = pcie_read8(segment, bus, device, 0, PCI_REG_HEADER_TYPE);
  uint8_t num_funcs = (header_type & PCI_HEADER_TYPE_MULTIFUNC) ? 8 : 1;

  for (uint8_t func = 0; func < num_funcs; func++) {
    pcie_probe_function(segment, bus, device, func);
  }
}

void pcie_scan_all(void) {
  g_device_count = 0;

  for (size_t i = 0; i < g_segment_count; i++) {
    uint16_t seg = g_segments[i].segment_group;
    uint8_t start = g_segments[i].start_bus;
    uint8_t end = g_segments[i].end_bus;

    for (uint32_t bus = start; bus <= end; bus++) {
      for (uint8_t dev = 0; dev < 32; dev++) {
        pcie_probe_device(seg, (uint8_t)bus, dev);
      }
    }
  }

  kprintf("[PCIe] %u devices found\n", (uint32_t)g_device_count);
}

size_t pcie_get_device_count(void) {
  return g_device_count;
}

const PciDevice *pcie_get_device(size_t index) {
  if (index >= g_device_count) {
    return NULL;
  }
  return &g_devices[index];
}

const PciDevice *pcie_find_device(uint16_t vendor_id, uint16_t device_id) {
  for (size_t i = 0; i < g_device_count; i++) {
    if (g_devices[i].vendor_id == vendor_id && g_devices[i].device_id == device_id) {
      return &g_devices[i];
    }
  }
  return NULL;
}

const PciDevice *pcie_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if) {
  for (size_t i = 0; i < g_device_count; i++) {
    if (g_devices[i].class_code == class_code &&
      g_devices[i].subclass == subclass &&
      (prog_if == 0xFF || g_devices[i].prog_if == prog_if)) {
      return &g_devices[i];
      }
  }
  return NULL;
}

uint8_t pcie_find_capability(const PciDevice *dev, uint8_t cap_id) {
  uint16_t status = pcie_read16(dev->segment, dev->bus, dev->device, dev->function, PCI_REG_STATUS);
  if (!(status & PCI_STATUS_CAPABILITIES)) {
    return 0;
  }

  uint8_t cap_ptr = pcie_read8(dev->segment, dev->bus, dev->device, dev->function, PCI_REG_CAPABILITIES) & ~0x3;
  while (cap_ptr >= 0x40 && cap_ptr <= 0xFC) {
    uint8_t current_id = pcie_read8(dev->segment, dev->bus, dev->device, dev->function, cap_ptr);
    if (current_id == cap_id) {
      return cap_ptr;
    }
    cap_ptr = pcie_read8(dev->segment, dev->bus, dev->device, dev->function, cap_ptr + 1) & ~0x3;
  }

  return 0;
}

uint16_t pcie_find_extended_capability(const PciDevice *dev, uint16_t cap_id) {
  uint16_t offset = 0x100;

  while (offset >= 0x100 && offset <= 0xFFC) {
    uint32_t header = pcie_read32(dev->segment, dev->bus, dev->device, dev->function, offset);
    if (header == 0 || header == 0xFFFFFFFF) {
      break;
    }

    uint16_t current_id = (uint16_t)(header & 0xFFFF);
    if (current_id == cap_id) {
      return offset;
    }

    uint16_t next_offset = (uint16_t)((header >> 20) & 0xFFF);
    if (next_offset == 0 || next_offset <= offset) {
      break;
    }
    offset = next_offset;
  }

  return 0;
}

void pcie_init(void) {
  size_t count = 0;
  const AcpiMcfgAllocation *allocs = acpi_get_mcfg_allocations(&count);
  if (!allocs || count == 0) {
    kprintf("[PCIe] Error: No MCFG table found\n");
    return;
  }

  g_segments = (AcpiMcfgAllocation *)kmalloc(count * sizeof(AcpiMcfgAllocation));
  if (!g_segments) {
    kprintf("[PCIe] Error: Failed to allocate segment cache\n");
    return;
  }

  for (size_t i = 0; i < count; i++) {
    g_segments[i] = allocs[i];
  }
  g_segment_count = count;

  pcie_scan_all();
}

void pcie_dump_devices(void) {
  for (size_t i = 0; i < g_device_count; i++) {
    const PciDevice *dev = &g_devices[i];
    kprintf("[%02u:%02u.%u] ID: %04x:%04x | Class: 0x%02x (Sub: 0x%02x, IF: 0x%02x)\n",
            (uint32_t)dev->bus, (uint32_t)dev->device, (uint32_t)dev->function,
            (uint32_t)dev->vendor_id, (uint32_t)dev->device_id,
            (uint32_t)dev->class_code, (uint32_t)dev->subclass, (uint32_t)dev->prog_if
    );

    for (uint8_t b = 0; b < dev->bar_count; b++) {
      if (dev->bars[b].size > 0) {
        kprintf("    BAR%u: %s at 0x%016llx (size: 0x%llx)\n",
                (uint32_t)b,
                dev->bars[b].is_io ? "I/O" : (dev->bars[b].is_64bit ? "MEM64" : "MEM32"),
                dev->bars[b].phys_addr,
                dev->bars[b].size
        );
      }
    }
  }
}


void pcie_enable_bus_master(const PciDevice *dev) {
  uint16_t cmd = pcie_read16(dev->segment, dev->bus, dev->device, dev->function, PCI_REG_COMMAND);
  cmd |= (PCI_CMD_BUS_MASTER | PCI_CMD_MEM_ENABLE);
  pcie_write16(dev->segment, dev->bus, dev->device, dev->function, PCI_REG_COMMAND, cmd);
}

int pcie_enable_msi(const PciDevice *dev, uint8_t vector, uint32_t lapic_id) {
  uint8_t cap = pcie_find_capability(dev, PCI_CAP_ID_MSI);
  if (!cap) {
    return -1;
  }

  uint16_t ctrl = pcie_read16(dev->segment, dev->bus, dev->device, dev->function, cap + 2);
  uint32_t msg_addr = 0xFEE00000U | (lapic_id << 12);
  uint16_t msg_data = vector;

  pcie_write32(dev->segment, dev->bus, dev->device, dev->function, cap + 4, msg_addr);

  if (ctrl & PCI_MSI_CTRL_64BIT) {
    pcie_write32(dev->segment, dev->bus, dev->device, dev->function, cap + 8, 0);
    pcie_write16(dev->segment, dev->bus, dev->device, dev->function, cap + 12, msg_data);
  } else {
    pcie_write16(dev->segment, dev->bus, dev->device, dev->function, cap + 8, msg_data);
  }

  ctrl |= PCI_MSI_CTRL_ENABLE;
  pcie_write16(dev->segment, dev->bus, dev->device, dev->function, cap + 2, ctrl);

  pcie_enable_bus_master(dev);
  return 0;
}

int pcie_disable_msi(const PciDevice *dev) {
  uint8_t cap = pcie_find_capability(dev, PCI_CAP_ID_MSI);
  if (!cap) {
    return -1;
  }

  uint16_t ctrl = pcie_read16(dev->segment, dev->bus, dev->device, dev->function, cap + 2);
  ctrl &= ~PCI_MSI_CTRL_ENABLE;
  pcie_write16(dev->segment, dev->bus, dev->device, dev->function, cap + 2, ctrl);
  return 0;
}

uint16_t pcie_get_msix_table_size(const PciDevice *dev) {
  uint8_t cap = pcie_find_capability(dev, PCI_CAP_ID_MSIX);
  if (!cap) {
    return 0;
  }

  uint16_t ctrl = pcie_read16(dev->segment, dev->bus, dev->device, dev->function, cap + 2);
  return (ctrl & PCI_MSIX_CTRL_TABLE_SIZE) + 1;
}

static volatile PciMsixTableEntry *pcie_get_msix_entry(const PciDevice *dev, uint16_t table_index) {
  uint8_t cap = pcie_find_capability(dev, PCI_CAP_ID_MSIX);
  if (!cap) {
    return NULL;
  }

  uint16_t table_size = pcie_get_msix_table_size(dev);
  if (table_index >= table_size) {
    return NULL;
  }

  uint32_t table_info = pcie_read32(dev->segment, dev->bus, dev->device, dev->function, cap + 4);
  uint8_t bir = (uint8_t)(table_info & 0x7);
  uint32_t table_offset = table_info & ~0x7;

  if (bir >= dev->bar_count || dev->bars[bir].phys_addr == 0) {
    return NULL;
  }

  uint64_t entry_phys = dev->bars[bir].phys_addr + table_offset + (table_index * sizeof(PciMsixTableEntry));
  uint64_t page_phys = entry_phys & ~(PAGE_SIZE - 1);
  uint64_t page_virt = page_phys + HHDM_BASE;

  PageDirectory pml4 = vmm_get_kernel_pml4();
  if (!vmm_virt_to_phys(pml4, page_virt)) {
    if (vmm_map_page(pml4, page_virt, page_phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NO_CACHE) != 0) {
      return NULL;
    }
  }

  uint64_t entry_virt = entry_phys + HHDM_BASE;
  return (volatile PciMsixTableEntry *)entry_virt;
}

int pcie_enable_msix_vector(const PciDevice *dev, uint16_t table_index, uint8_t vector, uint32_t lapic_id) {
  volatile PciMsixTableEntry *entry = pcie_get_msix_entry(dev, table_index);
  if (!entry) {
    return -1;
  }

  uint32_t msg_addr = 0xFEE00000U | (lapic_id << 12);
  entry->msg_addr_low = msg_addr;
  entry->msg_addr_high = 0;
  entry->msg_data = vector;
  entry->vector_ctrl &= ~PCI_MSIX_ENTRY_MASKED;

  return 0;
}

int pcie_mask_msix_vector(const PciDevice *dev, uint16_t table_index, int masked) {
  volatile PciMsixTableEntry *entry = pcie_get_msix_entry(dev, table_index);
  if (!entry) {
    return -1;
  }

  if (masked) {
    entry->vector_ctrl |= PCI_MSIX_ENTRY_MASKED;
  } else {
    entry->vector_ctrl &= ~PCI_MSIX_ENTRY_MASKED;
  }

  return 0;
}

int pcie_enable_msix(const PciDevice *dev) {
  uint8_t cap = pcie_find_capability(dev, PCI_CAP_ID_MSIX);
  if (!cap) {
    return -1;
  }

  pcie_disable_msi(dev);

  uint16_t ctrl = pcie_read16(dev->segment, dev->bus, dev->device, dev->function, cap + 2);
  ctrl |= PCI_MSIX_CTRL_ENABLE;
  ctrl &= ~PCI_MSIX_CTRL_MASK_ALL;
  pcie_write16(dev->segment, dev->bus, dev->device, dev->function, cap + 2, ctrl);

  pcie_enable_bus_master(dev);
  return 0;
}

int pcie_disable_msix(const PciDevice *dev) {
  uint8_t cap = pcie_find_capability(dev, PCI_CAP_ID_MSIX);
  if (!cap) {
    return -1;
  }

  uint16_t ctrl = pcie_read16(dev->segment, dev->bus, dev->device, dev->function, cap + 2);
  ctrl &= ~PCI_MSIX_CTRL_ENABLE;
  pcie_write16(dev->segment, dev->bus, dev->device, dev->function, cap + 2, ctrl);
  return 0;
}
