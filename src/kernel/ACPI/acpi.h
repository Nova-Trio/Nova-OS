#pragma once
#include <stdint.h>
#include <stddef.h>
#include <bootinfo.h>

typedef struct {
  char signature[8];
  uint8_t checksum;
  char oem_id[6];
  uint8_t revision;
  uint32_t rsdt_address;
  uint32_t length;
  uint64_t xsdt_address;
  uint8_t extended_checksum;
  uint8_t reserved[3];
} __attribute__((packed)) AcpiRsdp;

typedef struct {
  char signature[4];
  uint32_t length;
  uint8_t revision;
  uint8_t checksum;
  char oem_id[6];
  char oem_table_id[8];
  uint32_t oem_revision;
  uint32_t creator_id;
  uint32_t creator_revision;
} __attribute__((packed)) AcpiSdtHeader;

typedef struct {
  AcpiSdtHeader header;
  uint64_t tables[];
} __attribute__((packed)) AcpiXsdt;

typedef struct {
  uint8_t address_space;
  uint8_t bit_width;
  uint8_t bit_offset;
  uint8_t access_size;
  uint64_t address;
} __attribute__((packed)) AcpiGas;

typedef struct {
  AcpiSdtHeader header;
  uint32_t event_timer_block_id;
  AcpiGas base_address;
  uint8_t hpet_number;
  uint16_t main_counter_min_tick;
  uint8_t page_protection;
} __attribute__((packed)) AcpiHpetTable;

typedef struct {
  AcpiSdtHeader header;
  uint32_t firmware_ctrl;
  uint32_t dsdt;
  uint8_t reserved;
  uint8_t preferred_pm_profile;
  uint16_t sci_int;
  uint32_t smi_cmd;
  uint8_t acpi_enable;
  uint8_t acpi_disable;
  uint8_t s4bios_req;
  uint8_t pstate_cnt;
  uint32_t pm1a_evt_blk;
  uint32_t pm1b_evt_blk;
  uint32_t pm1a_cnt_blk;
  uint32_t pm1b_cnt_blk;
  uint32_t pm2_cnt_blk;
  uint32_t pm_tmr_blk;
  uint32_t gpe0_blk;
  uint32_t gpe1_blk;
  uint8_t pm1_evt_len;
  uint8_t pm1_cnt_len;
  uint8_t pm2_cnt_len;
  uint8_t pm_tmr_len;
  uint8_t gpe0_blk_len;
  uint8_t gpe1_blk_len;
  uint8_t gpe1_base;
  uint8_t cst_cnt;
  uint16_t p_lvl2_lat;
  uint16_t p_lvl3_lat;
  uint16_t flush_size;
  uint16_t flush_stride;
  uint8_t duty_offset;
  uint8_t duty_width;
  uint8_t day_alrm;
  uint8_t mon_alrm;
  uint8_t century;
  uint16_t iapc_boot_arch;
  uint8_t reserved1;
  uint32_t flags;
  AcpiGas reset_reg;
  uint8_t reset_value;
  uint16_t arm_boot_arch;
  uint8_t fadt_minor_version;
  uint64_t x_firmware_ctrl;
  uint64_t x_dsdt;
  AcpiGas x_pm1a_evt_blk;
  AcpiGas x_pm1b_evt_blk;
  AcpiGas x_pm1a_cnt_blk;
  AcpiGas x_pm1b_cnt_blk;
  AcpiGas x_pm2_cnt_blk;
  AcpiGas x_pm_tmr_blk;
  AcpiGas x_gpe0_blk;
  AcpiGas x_gpe1_blk;
} __attribute__((packed)) AcpiFadt;

typedef struct {
  AcpiSdtHeader header;
  uint32_t lapic_address;
  uint32_t flags;
} __attribute__((packed)) AcpiMadt;

typedef struct {
  uint8_t type;
  uint8_t length;
} __attribute__((packed)) AcpiMadtRecordHeader;

typedef struct {
  AcpiMadtRecordHeader header;
  uint16_t reserved;
  uint64_t phys_address;
} __attribute__((packed)) AcpiMadtLapicAddressOverride;

typedef struct {
  uint64_t base_address;
  uint16_t segment_group;
  uint8_t start_bus;
  uint8_t end_bus;
  uint32_t reserved;
} __attribute__((packed)) AcpiMcfgAllocation;

typedef struct {
  AcpiSdtHeader header;
  uint64_t reserved;
  AcpiMcfgAllocation allocations[];
} __attribute__((packed)) AcpiMcfg;

void acpi_init(void *rsdp_phys);
AcpiSdtHeader *acpi_find_table(const char *signature, size_t index);
const AcpiMcfgAllocation *acpi_get_mcfg_allocations(size_t *count);

void acpi_dump_tables(void);


void acpi_reboot(void) __attribute__((noreturn));
