#pragma once
#include <stdint.h>
#include <stddef.h>
#include <nvme.h>

#define GPT_SIGNATURE 0x5452415020494645ULL

#define GPT_ATTR_REQUIRED (1ULL << 0)
#define GPT_ATTR_NO_BLOCK_IO_BOOT (1ULL << 1)
#define GPT_ATTR_LEGACY_BIOS_BOOT (1ULL << 2)

typedef struct {
  uint8_t data[16];
} __attribute__((packed)) GptGuid;

typedef struct {
  uint64_t signature;
  uint32_t revision;
  uint32_t header_size;
  uint32_t header_crc32;
  uint32_t reserved;
  uint64_t current_lba;
  uint64_t backup_lba;
  uint64_t first_usable_lba;
  uint64_t last_usable_lba;
  GptGuid disk_guid;
  uint64_t partition_entries_lba;
  uint32_t num_partition_entries;
  uint32_t sizeof_partition_entry;
  uint32_t partition_entry_array_crc32;
} __attribute__((packed)) GptHeader;

typedef struct {
  GptGuid type_guid;
  GptGuid unique_guid;
  uint64_t starting_lba;
  uint64_t ending_lba;
  uint64_t attributes;
  uint16_t name[36];
} __attribute__((packed)) GptRawEntry;

typedef struct {
  GptGuid type_guid;
  GptGuid unique_guid;
  uint64_t starting_lba;
  uint64_t ending_lba;
  uint64_t sector_count;
  uint64_t attributes;
  char name[37];
} GptPartition;

typedef struct {
  GptPartition *partitions;
  size_t partition_count;
  uint32_t block_size;
  uint64_t first_usable_lba;
  uint64_t last_usable_lba;
  GptGuid disk_guid;
} GptTable;

int gpt_parse(NvmeController *ctrl, uint32_t nsid, GptTable *table);
void gpt_free(GptTable *table);
void gpt_dump(const GptTable *table);

int gpt_is_esp(const GptPartition *part);
int gpt_is_basic_data(const GptPartition *part);
const GptPartition *gpt_find_esp(const GptTable *table);
const GptPartition *gpt_find_basic_data(const GptTable *table);
