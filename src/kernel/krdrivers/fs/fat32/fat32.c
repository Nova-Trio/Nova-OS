#include "fat32.h"
#include <heap.h>
#include <console.h>
#include <string.h>

#define FAT32_SIGNATURE_55AA 0xAA55

#define FAT32_CLUSTER_FREE 0x00000000U
#define FAT32_CLUSTER_BAD 0x0FFFFFF7U
#define FAT32_CLUSTER_EOC 0x0FFFFFF8U
#define FAT32_CLUSTER_MASK 0x0FFFFFFFU

#define FAT_ATTR_LFN (FAT_ATTR_READ_ONLY | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM | FAT_ATTR_VOLUME_ID)

#define FAT_ENTRY_FREE 0xE5
#define FAT_ENTRY_END 0x00
#define FAT_LFN_LAST_MASK 0x40
#define FAT_LFN_SEQ_MASK 0x1F

typedef struct {
  uint8_t jmp_boot[3];
  char oem_name[8];
  uint16_t bytes_per_sector;
  uint8_t sectors_per_cluster;
  uint16_t reserved_sector_count;
  uint8_t num_fats;
  uint16_t root_entry_count;
  uint16_t total_sectors_16;
  uint8_t media_type;
  uint16_t fat_size_16;
  uint16_t sectors_per_track;
  uint16_t num_heads;
  uint32_t hidden_sectors;
  uint32_t total_sectors_32;
  uint32_t fat_size_32;
  uint16_t ext_flags;
  uint16_t fs_version;
  uint32_t root_cluster;
  uint16_t fs_info_sector;
  uint16_t backup_boot_sector;
  uint8_t reserved[12];
  uint8_t drive_number;
  uint8_t reserved1;
  uint8_t boot_signature;
  uint32_t volume_id;
  char volume_label[11];
  char fs_type[8];
} __attribute__((packed)) Fat32Bpb;

typedef struct {
  uint8_t name[11];
  uint8_t attr;
  uint8_t nt_reserved;
  uint8_t creation_time_tenth;
  uint16_t creation_time;
  uint16_t creation_date;
  uint16_t last_access_date;
  uint16_t cluster_high;
  uint16_t write_time;
  uint16_t write_date;
  uint16_t cluster_low;
  uint32_t file_size;
} __attribute__((packed)) Fat32RawDirEntry;

typedef struct {
  uint8_t order;
  uint16_t name1[5];
  uint8_t attr;
  uint8_t type;
  uint8_t checksum;
  uint16_t name2[6];
  uint16_t cluster_low;
  uint16_t name3[2];
} __attribute__((packed)) Fat32RawLfnEntry;

static Fat32Volume g_root_volume;
static int g_root_mounted = 0;

static void trim_trailing_spaces(char *dst, const char *src, size_t len) {
  memcpy(dst, src, len);
  dst[len] = '\0';
  while (len > 0 && (dst[len - 1] == ' ' || dst[len - 1] == '\0')) {
    dst[len - 1] = '\0';
    len--;
  }
}

static char to_lower(char c) {
  if (c >= 'A' && c <= 'Z') {
    return (char)(c + ('a' - 'A'));
  }
  return c;
}

static int strcasecmp_ascii(const char *s1, const char *s2) {
  while (*s1 && *s2) {
    if (to_lower(*s1) != to_lower(*s2)) {
      return (int)((unsigned char)to_lower(*s1) - (unsigned char)to_lower(*s2));
    }
    s1++;
    s2++;
  }
  return (int)((unsigned char)to_lower(*s1) - (unsigned char)to_lower(*s2));
}

static uint8_t calculate_sfn_checksum(const uint8_t *sfn) {
  uint8_t sum = 0;
  for (size_t i = 0; i < 11; i++) {
    sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + sfn[i]);
  }
  return sum;
}

static void format_sfn_name(const Fat32RawDirEntry *sfn, char *out_name) {
  char base[9];
  char ext[4];
  size_t base_len = 0;
  size_t ext_len = 0;

  for (size_t i = 0; i < 8; i++) {
    uint8_t c = sfn->name[i];
    if (i == 0 && c == 0x05) c = 0xE5;
    if (c == ' ') break;
    if (sfn->nt_reserved & 0x08) {
      base[base_len++] = to_lower((char)c);
    } else {
      base[base_len++] = (char)c;
    }
  }
  base[base_len] = '\0';

  for (size_t i = 0; i < 3; i++) {
    uint8_t c = sfn->name[8 + i];
    if (c == ' ') break;
    if (sfn->nt_reserved & 0x10) {
      ext[ext_len++] = to_lower((char)c);
    } else {
      ext[ext_len++] = (char)c;
    }
  }
  ext[ext_len] = '\0';

  size_t out_idx = 0;
  for (size_t i = 0; i < base_len; i++) {
    out_name[out_idx++] = base[i];
  }

  if (ext_len > 0) {
    out_name[out_idx++] = '.';
    for (size_t i = 0; i < ext_len; i++) {
      out_name[out_idx++] = ext[i];
    }
  }

  out_name[out_idx] = '\0';
}

static void utf16le_to_utf8(const uint16_t *src, char *dst, size_t max_out) {
  size_t in_idx = 0;
  size_t out_idx = 0;

  while (src[in_idx] != 0 && src[in_idx] != 0xFFFF && out_idx + 4 < max_out) {
    uint16_t ch = src[in_idx++];
    if (ch < 0x80) {
      dst[out_idx++] = (char)ch;
    } else if (ch < 0x800) {
      dst[out_idx++] = (char)(0xC0 | (ch >> 6));
      dst[out_idx++] = (char)(0x80 | (ch & 0x3F));
    } else {
      dst[out_idx++] = (char)(0xE0 | (ch >> 12));
      dst[out_idx++] = (char)(0x80 | ((ch >> 6) & 0x3F));
      dst[out_idx++] = (char)(0x80 | (ch & 0x3F));
    }
  }

  dst[out_idx] = '\0';
}

static uint64_t cluster_to_lba(const Fat32Volume *volume, uint32_t cluster) {
  if (!volume || cluster < 2) return 0;
  return volume->data_start_lba + ((uint64_t)(cluster - 2) * volume->sectors_per_cluster);
}

static int get_next_cluster(Fat32Volume *volume, uint32_t cluster, uint32_t *next_cluster) {
  if (!volume || cluster < 2 || cluster >= (volume->total_clusters + 2) || !next_cluster) {
    return -1;
  }

  uint32_t fat_offset = cluster * 4;
  uint64_t fat_sector_idx = fat_offset / volume->bytes_per_sector;
  uint32_t byte_offset = fat_offset % volume->bytes_per_sector;

  if (fat_sector_idx >= volume->fat_size) {
    return -1;
  }

  uint64_t target_lba = volume->fat_start_lba + fat_sector_idx;

  if (volume->fat_cache_lba != target_lba) {
    if (nvme_read(volume->ctrl, volume->nsid, target_lba, 1, volume->fat_cache) != 0) {
      volume->fat_cache_lba = ~0ULL;
      return -1;
    }
    volume->fat_cache_lba = target_lba;
  }

  uint32_t val = *(uint32_t *)(volume->fat_cache + byte_offset);
  *next_cluster = val & FAT32_CLUSTER_MASK;
  return 0;
}

static int read_cluster(Fat32Volume *volume, uint32_t cluster, void *buf) {
  if (!volume || !buf || cluster < 2 || cluster >= (volume->total_clusters + 2)) {
    return -1;
  }

  uint64_t lba = cluster_to_lba(volume, cluster);
  return nvme_read(volume->ctrl, volume->nsid, lba, volume->sectors_per_cluster, buf);
}

static int read_cluster_chain(Fat32Volume *volume, uint32_t start_cluster, void **out_buf, size_t *out_size) {
  if (!volume || !out_buf || !out_size || start_cluster < 2) {
    return -1;
  }

  size_t cluster_count = 0;
  uint32_t curr = start_cluster;

  while (curr >= 2 && curr < FAT32_CLUSTER_EOC) {
    if (curr == FAT32_CLUSTER_BAD) {
      return -1;
    }

    cluster_count++;
    if (cluster_count > volume->total_clusters) {
      return -1;
    }

    uint32_t next = 0;
    if (get_next_cluster(volume, curr, &next) != 0) {
      return -1;
    }
    curr = next;
  }

  if (cluster_count == 0) {
    *out_buf = NULL;
    *out_size = 0;
    return 0;
  }

  size_t total_bytes = cluster_count * volume->bytes_per_cluster;
  uint8_t *buffer = (uint8_t *)kmalloc(total_bytes);
  if (!buffer) {
    return -1;
  }

  curr = start_cluster;
  size_t idx = 0;

  while (curr >= 2 && curr < FAT32_CLUSTER_EOC && idx < cluster_count) {
    if (read_cluster(volume, curr, buffer + (idx * volume->bytes_per_cluster)) != 0) {
      kfree(buffer);
      return -1;
    }

    idx++;
    uint32_t next = 0;
    if (get_next_cluster(volume, curr, &next) != 0) {
      kfree(buffer);
      return -1;
    }
    curr = next;
  }

  *out_buf = buffer;
  *out_size = total_bytes;
  return 0;
}

static int list_dir_cluster(Fat32Volume *volume, uint32_t cluster, Fat32DirCallback callback, void *context) {
  if (!volume || !callback || cluster < 2) {
    return -1;
  }

  void *dir_buf = NULL;
  size_t dir_size = 0;

  if (read_cluster_chain(volume, cluster, &dir_buf, &dir_size) != 0 || !dir_buf) {
    return -1;
  }

  size_t num_entries = dir_size / sizeof(Fat32RawDirEntry);
  Fat32RawDirEntry *entries = (Fat32RawDirEntry *)dir_buf;

  uint16_t lfn_unicode[260];
  memset(lfn_unicode, 0, sizeof(lfn_unicode));
  uint8_t lfn_checksum = 0;
  int lfn_active = 0;

  for (size_t i = 0; i < num_entries; i++) {
    Fat32RawDirEntry *raw = &entries[i];

    if (raw->name[0] == FAT_ENTRY_END) {
      break;
    }

    if (raw->name[0] == FAT_ENTRY_FREE) {
      lfn_active = 0;
      continue;
    }

    if (raw->attr == FAT_ATTR_LFN) {
      Fat32RawLfnEntry *lfn = (Fat32RawLfnEntry *)raw;
      uint8_t seq = lfn->order & FAT_LFN_SEQ_MASK;

      if (seq >= 1 && seq <= 20) {
        size_t char_offset = (size_t)(seq - 1) * 13;

        for (size_t c = 0; c < 5; c++) {
          lfn_unicode[char_offset + c] = lfn->name1[c];
        }
        for (size_t c = 0; c < 6; c++) {
          lfn_unicode[char_offset + 5 + c] = lfn->name2[c];
        }
        for (size_t c = 0; c < 2; c++) {
          lfn_unicode[char_offset + 11 + c] = lfn->name3[c];
        }

        if (lfn->order & FAT_LFN_LAST_MASK) {
          lfn_unicode[char_offset + 13] = 0;
          lfn_checksum = lfn->checksum;
          lfn_active = 1;
        }
      }
      continue;
    }

    if (raw->attr & FAT_ATTR_VOLUME_ID) {
      lfn_active = 0;
      continue;
    }

    Fat32DirEntry entry;
    memset(&entry, 0, sizeof(entry));

    if (lfn_active && lfn_checksum == calculate_sfn_checksum(raw->name)) {
      utf16le_to_utf8(lfn_unicode, entry.name, sizeof(entry.name));
    } else {
      format_sfn_name(raw, entry.name);
    }

    lfn_active = 0;
    memset(lfn_unicode, 0, sizeof(lfn_unicode));

    entry.start_cluster = ((uint32_t)raw->cluster_high << 16) | (uint32_t)raw->cluster_low;
    entry.file_size = raw->file_size;
    entry.attributes = raw->attr;
    entry.is_directory = (raw->attr & FAT_ATTR_DIRECTORY) != 0;

    callback(&entry, context);
  }

  kfree(dir_buf);
  return 0;
}

typedef struct {
  const char *target_name;
  Fat32DirEntry *out_entry;
  int found;
} FindContext;

static void find_callback(const Fat32DirEntry *entry, void *context) {
  FindContext *ctx = (FindContext *)context;
  if (!ctx->found && strcasecmp_ascii(entry->name, ctx->target_name) == 0) {
    *ctx->out_entry = *entry;
    ctx->found = 1;
  }
}

static int find_in_dir(Fat32Volume *volume, uint32_t cluster, const char *name, Fat32DirEntry *out_entry) {
  if (!volume || !name || !out_entry || cluster < 2) {
    return -1;
  }

  FindContext ctx;
  ctx.target_name = name;
  ctx.out_entry = out_entry;
  ctx.found = 0;

  if (list_dir_cluster(volume, cluster, find_callback, &ctx) != 0) {
    return -1;
  }

  return ctx.found ? 0 : -1;
}

int fat32_mount(NvmeController *ctrl, uint32_t nsid, const GptPartition *partition, Fat32Volume *volume) {
  if (!ctrl || !partition || !volume) {
    return -1;
  }

  NvmeNamespace *ns = ctrl->namespaces;
  while (ns && ns->nsid != nsid) {
    ns = ns->next;
  }

  if (!ns || ns->block_size == 0) {
    kprintf("[FAT32] Error: Invalid NVMe namespace %u\n", nsid);
    return -1;
  }

  uint8_t *sector_buf = (uint8_t *)kmalloc(ns->block_size);
  if (!sector_buf) {
    kprintf("[FAT32] Error: Failed to allocate boot sector buffer\n");
    return -1;
  }

  if (nvme_read(ctrl, nsid, partition->starting_lba, 1, sector_buf) != 0) {
    kprintf("[FAT32] Error: Failed to read Boot Sector at LBA %llu\n", partition->starting_lba);
    kfree(sector_buf);
    return -1;
  }

  uint16_t sig = *(uint16_t *)(sector_buf + 510);
  if (sig != FAT32_SIGNATURE_55AA) {
    kprintf("[FAT32] Error: Invalid boot signature 0x%04x\n", sig);
    kfree(sector_buf);
    return -1;
  }

  Fat32Bpb *bpb = (Fat32Bpb *)sector_buf;

  if (bpb->bytes_per_sector == 0 || (bpb->bytes_per_sector % 512) != 0) {
    kprintf("[FAT32] Error: Unsupported sector size: %u bytes\n", bpb->bytes_per_sector);
    kfree(sector_buf);
    return -1;
  }

  if (bpb->sectors_per_cluster == 0 || (bpb->sectors_per_cluster & (bpb->sectors_per_cluster - 1)) != 0) {
    kprintf("[FAT32] Error: Invalid sectors per cluster: %u\n", bpb->sectors_per_cluster);
    kfree(sector_buf);
    return -1;
  }

  uint32_t total_sectors = bpb->total_sectors_16 ? bpb->total_sectors_16 : bpb->total_sectors_32;
  uint32_t fat_size = bpb->fat_size_16 ? bpb->fat_size_16 : bpb->fat_size_32;

  if (fat_size == 0 || bpb->num_fats == 0) {
    kprintf("[FAT32] Error: Invalid FAT table count (%u) or size (%u)\n", bpb->num_fats, fat_size);
    kfree(sector_buf);
    return -1;
  }

  uint32_t root_dir_sectors = ((bpb->root_entry_count * 32) + (bpb->bytes_per_sector - 1)) / bpb->bytes_per_sector;
  uint32_t data_sectors = total_sectors - (bpb->reserved_sector_count + (bpb->num_fats * fat_size) + root_dir_sectors);
  uint32_t total_clusters = data_sectors / bpb->sectors_per_cluster;

  if (total_clusters < 65525) {
    kprintf("[FAT32] Error: Volume is not FAT32 (cluster count %u)\n", total_clusters);
    kfree(sector_buf);
    return -1;
  }

  volume->fat_cache = (uint8_t *)kmalloc(bpb->bytes_per_sector);
  if (!volume->fat_cache) {
    kprintf("[FAT32] Error: Failed to allocate FAT sector cache\n");
    kfree(sector_buf);
    return -1;
  }
  volume->fat_cache_lba = ~0ULL;

  volume->ctrl = ctrl;
  volume->nsid = nsid;
  volume->partition_start_lba = partition->starting_lba;
  volume->partition_sector_count = partition->sector_count;
  volume->bytes_per_sector = bpb->bytes_per_sector;
  volume->sectors_per_cluster = bpb->sectors_per_cluster;
  volume->reserved_sector_count = bpb->reserved_sector_count;
  volume->num_fats = bpb->num_fats;
  volume->total_sectors = total_sectors;
  volume->fat_size = fat_size;
  volume->root_cluster = bpb->root_cluster;
  volume->fs_info_sector = bpb->fs_info_sector;
  volume->total_clusters = total_clusters;
  volume->bytes_per_cluster = (uint32_t)bpb->bytes_per_sector * bpb->sectors_per_cluster;

  volume->fat_start_lba = partition->starting_lba + bpb->reserved_sector_count;
  volume->data_start_lba = volume->fat_start_lba + ((uint64_t)bpb->num_fats * fat_size) + root_dir_sectors;

  trim_trailing_spaces(volume->volume_label, bpb->volume_label, 11);
  trim_trailing_spaces(volume->oem_name, bpb->oem_name, 8);

  kfree(sector_buf);
  return 0;
}

void fat32_unmount(Fat32Volume *volume) {
  if (volume) {
    if (volume->fat_cache) {
      kfree(volume->fat_cache);
      volume->fat_cache = NULL;
    }
    volume->fat_cache_lba = ~0ULL;
  }
}

int fat32_stat(Fat32Volume *volume, const char *path, Fat32DirEntry *out_entry) {
  if (!volume || !path || !out_entry) {
    return -1;
  }

  const char *p = path;
  while (*p == '/' || *p == '\\') {
    p++;
  }

  if (*p == '\0') {
    out_entry->name[0] = '/';
    out_entry->name[1] = '\0';
    out_entry->start_cluster = volume->root_cluster;
    out_entry->file_size = 0;
    out_entry->attributes = FAT_ATTR_DIRECTORY;
    out_entry->is_directory = 1;
    return 0;
  }

  uint32_t current_cluster = volume->root_cluster;
  Fat32DirEntry current_entry;
  memset(&current_entry, 0, sizeof(current_entry));

  while (*p != '\0') {
    const char *start = p;
    while (*p != '\0' && *p != '/' && *p != '\\') {
      p++;
    }

    size_t token_len = (size_t)(p - start);
    if (token_len == 0) {
      while (*p == '/' || *p == '\\') p++;
      continue;
    }

    char *token = (char *)kmalloc(token_len + 1);
    if (!token) {
      return -1;
    }
    memcpy(token, start, token_len);
    token[token_len] = '\0';

    while (*p == '/' || *p == '\\') {
      p++;
    }

    int is_last = (*p == '\0');

    if (find_in_dir(volume, current_cluster, token, &current_entry) != 0) {
      kfree(token);
      return -1;
    }

    kfree(token);

    if (!is_last) {
      if (!current_entry.is_directory) {
        return -1;
      }
      current_cluster = current_entry.start_cluster;
    }
  }

  *out_entry = current_entry;
  return 0;
}

int fat32_list_dir(Fat32Volume *volume, const char *path, Fat32DirCallback callback, void *context) {
  if (!volume || !callback) {
    return -1;
  }

  if (!path || path[0] == '\0' || (path[0] == '/' && path[1] == '\0') || (path[0] == '\\' && path[1] == '\0')) {
    return list_dir_cluster(volume, volume->root_cluster, callback, context);
  }

  Fat32DirEntry entry;
  if (fat32_stat(volume, path, &entry) != 0) {
    return -1;
  }

  if (!entry.is_directory) {
    return -1;
  }

  return list_dir_cluster(volume, entry.start_cluster, callback, context);
}

int fat32_read(Fat32Volume *volume, const char *path, uint64_t offset, size_t size, void *buf, size_t *bytes_read) {
  if (!volume || !path || !buf || !bytes_read) {
    return -1;
  }

  *bytes_read = 0;

  Fat32DirEntry entry;
  if (fat32_stat(volume, path, &entry) != 0) {
    return -1;
  }

  if (entry.is_directory) {
    return -1;
  }

  if (offset >= entry.file_size || size == 0) {
    return 0;
  }

  if (offset + size > entry.file_size) {
    size = (size_t)(entry.file_size - offset);
  }

  uint32_t cluster_size = volume->bytes_per_cluster;
  uint32_t start_cluster_idx = (uint32_t)(offset / cluster_size);
  uint32_t cluster_offset = (uint32_t)(offset % cluster_size);

  uint32_t curr_cluster = entry.start_cluster;
  for (uint32_t i = 0; i < start_cluster_idx; i++) {
    if (curr_cluster < 2 || curr_cluster >= FAT32_CLUSTER_EOC) {
      return -1;
    }
    if (get_next_cluster(volume, curr_cluster, &curr_cluster) != 0) {
      return -1;
    }
  }

  uint8_t *dest = (uint8_t *)buf;
  size_t remaining = size;
  uint8_t *temp_cluster = NULL;

  while (remaining > 0 && curr_cluster >= 2 && curr_cluster < FAT32_CLUSTER_EOC) {
    size_t chunk = cluster_size - cluster_offset;
    if (chunk > remaining) {
      chunk = remaining;
    }

    if (cluster_offset == 0 && chunk == cluster_size) {
      if (read_cluster(volume, curr_cluster, dest) != 0) {
        if (temp_cluster) kfree(temp_cluster);
        return -1;
      }
    } else {
      if (!temp_cluster) {
        temp_cluster = (uint8_t *)kmalloc(cluster_size);
        if (!temp_cluster) {
          return -1;
        }
      }

      if (read_cluster(volume, curr_cluster, temp_cluster) != 0) {
        kfree(temp_cluster);
        return -1;
      }

      memcpy(dest, temp_cluster + cluster_offset, chunk);
    }

    dest += chunk;
    remaining -= chunk;
    cluster_offset = 0;

    if (remaining > 0) {
      if (get_next_cluster(volume, curr_cluster, &curr_cluster) != 0) {
        if (temp_cluster) kfree(temp_cluster);
        return -1;
      }
    }
  }

  if (temp_cluster) {
    kfree(temp_cluster);
  }

  *bytes_read = size - remaining;
  return 0;
}

int fat32_read_file(Fat32Volume *volume, const char *path, void **out_buf, size_t *out_size) {
  if (!volume || !path || !out_buf || !out_size) {
    return -1;
  }

  Fat32DirEntry entry;
  if (fat32_stat(volume, path, &entry) != 0) {
    return -1;
  }

  if (entry.is_directory) {
    return -1;
  }

  if (entry.file_size == 0) {
    *out_buf = NULL;
    *out_size = 0;
    return 0;
  }

  void *buf = kmalloc(entry.file_size);
  if (!buf) {
    return -1;
  }

  size_t bytes_read = 0;
  if (fat32_read(volume, path, 0, entry.file_size, buf, &bytes_read) != 0 || bytes_read != entry.file_size) {
    kfree(buf);
    return -1;
  }

  *out_buf = buf;
  *out_size = bytes_read;
  return 0;
}

int fs_init(void) {
  if (g_root_mounted) {
    return 0;
  }

  for (NvmeController *ctrl = nvme_get_controllers(); ctrl; ctrl = ctrl->next) {
    for (NvmeNamespace *ns = ctrl->namespaces; ns; ns = ns->next) {
      GptTable gpt;
      if (gpt_parse(ctrl, ns->nsid, &gpt) != 0) {
        continue;
      }

      const GptPartition *target = gpt_find_esp(&gpt);
      if (!target) {
        target = gpt_find_basic_data(&gpt);
      }
      if (!target && gpt.partition_count > 0) {
        target = &gpt.partitions[0];
      }

      if (target) {
        if (fat32_mount(ctrl, ns->nsid, target, &g_root_volume) == 0) {
          g_root_mounted = 1;
          gpt_free(&gpt);
          kprintf("[FS] Mounted root volume: \"%s\" (%u MB)\n", g_root_volume.volume_label[0] ? g_root_volume.volume_label : "<NO NAME>",
                  (uint32_t)(((uint64_t)g_root_volume.total_sectors * g_root_volume.bytes_per_sector) / (1024 * 1024)));
          return 0;
        }
      }

      gpt_free(&gpt);
    }
  }

  kprintf("[FS] Error: No bootable FAT32 filesystem found\n");
  return -1;
}

int fs_read_file(const char *path, void **out_buf, size_t *out_size) {
  if (!g_root_mounted) return -1;
  return fat32_read_file(&g_root_volume, path, out_buf, out_size);
}

int fs_read(const char *path, uint64_t offset, size_t size, void *buf, size_t *bytes_read) {
  if (!g_root_mounted) return -1;
  return fat32_read(&g_root_volume, path, offset, size, buf, bytes_read);
}

int fs_list_dir(const char *path, Fat32DirCallback callback, void *context) {
  if (!g_root_mounted) return -1;
  return fat32_list_dir(&g_root_volume, path, callback, context);
}

int fs_stat(const char *path, Fat32DirEntry *out_entry) {
  if (!g_root_mounted) return -1;
  return fat32_stat(&g_root_volume, path, out_entry);
}

Fat32Volume *fs_get_root_volume(void) {
  return g_root_mounted ? &g_root_volume : NULL;
}
