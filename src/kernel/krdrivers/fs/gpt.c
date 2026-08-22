#include "gpt.h"
#include <heap.h>
#include <console.h>
#include <string.h>

static const GptGuid g_guid_esp = {
  .data = { 0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B }
};

static const GptGuid g_guid_basic_data = {
  .data = { 0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 }
};

static const GptGuid g_guid_zero = {
  .data = { 0 }
};

static uint32_t crc32(const void *data, size_t length) {
  const uint8_t *buf = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFU;

  for (size_t i = 0; i < length; i++) {
    crc ^= buf[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (0xEDB88320U & -(crc & 1));
    }
  }

  return ~crc;
}

static int guid_equal(const GptGuid *a, const GptGuid *b) {
  for (size_t i = 0; i < 16; i++) {
    if (a->data[i] != b->data[i]) {
      return 0;
    }
  }
  return 1;
}

static void utf16le_to_ascii(const uint16_t *src, char *dst, size_t max_chars) {
  size_t i = 0;
  for (; i < max_chars && src[i] != 0; i++) {
    uint16_t ch = src[i];
    dst[i] = (ch < 0x80) ? (char)ch : '?';
  }
  dst[i] = '\0';
}

int gpt_is_esp(const GptPartition *part) {
  if (!part) return 0;
  return guid_equal(&part->type_guid, &g_guid_esp);
}

int gpt_is_basic_data(const GptPartition *part) {
  if (!part) return 0;
  return guid_equal(&part->type_guid, &g_guid_basic_data);
}

const GptPartition *gpt_find_esp(const GptTable *table) {
  if (!table || !table->partitions) return NULL;
  for (size_t i = 0; i < table->partition_count; i++) {
    if (gpt_is_esp(&table->partitions[i])) {
      return &table->partitions[i];
    }
  }
  return NULL;
}

const GptPartition *gpt_find_basic_data(const GptTable *table) {
  if (!table || !table->partitions) return NULL;
  for (size_t i = 0; i < table->partition_count; i++) {
    if (gpt_is_basic_data(&table->partitions[i])) {
      return &table->partitions[i];
    }
  }
  return NULL;
}

int gpt_parse(NvmeController *ctrl, uint32_t nsid, GptTable *table) {
  if (!ctrl || !table) {
    return -1;
  }

  NvmeNamespace *ns = ctrl->namespaces;
  while (ns && ns->nsid != nsid) {
    ns = ns->next;
  }

  if (!ns || ns->block_size == 0) {
    kprintf("[GPT] Error: Namespace %u not found or invalid block size\n", nsid);
    return -1;
  }

  uint32_t block_size = ns->block_size;
  uint8_t *header_buf = (uint8_t *)kmalloc(block_size);
  if (!header_buf) {
    kprintf("[GPT] Error: Memory allocation failed for header buffer\n");
    return -1;
  }

  if (nvme_read(ctrl, nsid, 1, 1, header_buf) != 0) {
    kprintf("[GPT] Error: Failed to read LBA 1\n");
    kfree(header_buf);
    return -1;
  }

  GptHeader *hdr = (GptHeader *)header_buf;
  if (hdr->signature != GPT_SIGNATURE) {
    kprintf("[GPT] Error: Invalid GPT signature (0x%016llx)\n", hdr->signature);
    kfree(header_buf);
    return -1;
  }

  if (hdr->header_size < sizeof(GptHeader) || hdr->header_size > block_size) {
    kprintf("[GPT] Error: Invalid header size %u\n", hdr->header_size);
    kfree(header_buf);
    return -1;
  }

  uint32_t stored_crc = hdr->header_crc32;
  hdr->header_crc32 = 0;
  uint32_t calc_crc = crc32(hdr, hdr->header_size);
  hdr->header_crc32 = stored_crc;

  if (stored_crc != calc_crc) {
    kprintf("[GPT] Error: Header CRC mismatch (expected 0x%08x, got 0x%08x)\n", stored_crc, calc_crc);
    kfree(header_buf);
    return -1;
  }

  uint32_t num_entries = hdr->num_partition_entries;
  uint32_t entry_size = hdr->sizeof_partition_entry;
  uint64_t entries_lba = hdr->partition_entries_lba;
  uint32_t entries_crc = hdr->partition_entry_array_crc32;

  if (entry_size < sizeof(GptRawEntry) || num_entries == 0) {
    kprintf("[GPT] Error: Invalid partition entry parameters\n");
    kfree(header_buf);
    return -1;
  }

  uint64_t total_entry_bytes = (uint64_t)num_entries * entry_size;
  uint32_t entry_blocks = (uint32_t)((total_entry_bytes + block_size - 1) / block_size);

  uint8_t *entries_buf = (uint8_t *)kmalloc(entry_blocks * block_size);
  if (!entries_buf) {
    kprintf("[GPT] Error: Memory allocation failed for entries buffer\n");
    kfree(header_buf);
    return -1;
  }

  if (nvme_read(ctrl, nsid, entries_lba, entry_blocks, entries_buf) != 0) {
    kprintf("[GPT] Error: Failed to read partition entry array at LBA %llu\n", entries_lba);
    kfree(entries_buf);
    kfree(header_buf);
    return -1;
  }

  uint32_t calc_entries_crc = crc32(entries_buf, total_entry_bytes);
  if (entries_crc != calc_entries_crc) {
    kprintf("[GPT] Error: Partition array CRC mismatch (expected 0x%08x, got 0x%08x)\n", entries_crc, calc_entries_crc);
    kfree(entries_buf);
    kfree(header_buf);
    return -1;
  }

  size_t valid_count = 0;
  for (uint32_t i = 0; i < num_entries; i++) {
    GptRawEntry *raw = (GptRawEntry *)(entries_buf + (i * entry_size));
    if (!guid_equal(&raw->type_guid, &g_guid_zero)) {
      valid_count++;
    }
  }

  table->partitions = NULL;
  table->partition_count = 0;
  table->block_size = block_size;
  table->first_usable_lba = hdr->first_usable_lba;
  table->last_usable_lba = hdr->last_usable_lba;
  table->disk_guid = hdr->disk_guid;

  if (valid_count > 0) {
    table->partitions = (GptPartition *)kmalloc(valid_count * sizeof(GptPartition));
    if (!table->partitions) {
      kprintf("[GPT] Error: Failed to allocate partition list\n");
      kfree(entries_buf);
      kfree(header_buf);
      return -1;
    }

    size_t out_idx = 0;
    for (uint32_t i = 0; i < num_entries; i++) {
      GptRawEntry *raw = (GptRawEntry *)(entries_buf + (i * entry_size));
      if (guid_equal(&raw->type_guid, &g_guid_zero)) {
        continue;
      }

      GptPartition *p = &table->partitions[out_idx];
      p->type_guid = raw->type_guid;
      p->unique_guid = raw->unique_guid;
      p->starting_lba = raw->starting_lba;
      p->ending_lba = raw->ending_lba;
      p->sector_count = (raw->ending_lba >= raw->starting_lba) ? (raw->ending_lba - raw->starting_lba + 1) : 0;
      p->attributes = raw->attributes;
      utf16le_to_ascii(raw->name, p->name, 36);

      out_idx++;
    }

    table->partition_count = valid_count;
  }

  kfree(entries_buf);
  kfree(header_buf);
  return 0;
}

void gpt_free(GptTable *table) {
  if (table) {
    if (table->partitions) {
      kfree(table->partitions);
      table->partitions = NULL;
    }
    table->partition_count = 0;
  }
}

void gpt_dump(const GptTable *table) {
  if (!table) return;

  kprintf("[GPT] %u valid partitions (Block size: %u bytes)\n",
          (uint32_t)table->partition_count, table->block_size);

  for (size_t i = 0; i < table->partition_count; i++) {
    const GptPartition *p = &table->partitions[i];
    const char *type_str = "Unknown";
    if (gpt_is_esp(p)) {
      type_str = "EFI System Partition";
    } else if (gpt_is_basic_data(p)) {
      type_str = "Basic Data";
    }

    uint64_t size_mb = (p->sector_count * table->block_size) / (1024 * 1024);

    kprintf("[GPT]  #%u: \"%s\" | %s | LBA %llu..%llu (%llu MB)\n",
            (uint32_t)i,
            p->name[0] ? p->name : "<unnamed>",
            type_str,
            p->starting_lba,
            p->ending_lba,
            size_mb
    );
  }
}
