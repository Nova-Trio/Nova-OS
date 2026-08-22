#pragma once
#include <stdint.h>
#include <stddef.h>
#include <nvme.h>
#include <gpt.h>

#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN 0x02
#define FAT_ATTR_SYSTEM 0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE 0x20

typedef struct {
  char name[256];
  uint32_t start_cluster;
  uint32_t file_size;
  uint8_t attributes;
  uint8_t is_directory;
} Fat32DirEntry;

typedef struct {
  NvmeController *ctrl;
  uint32_t nsid;
  uint64_t partition_start_lba;
  uint64_t partition_sector_count;

  uint16_t bytes_per_sector;
  uint8_t sectors_per_cluster;
  uint16_t reserved_sector_count;
  uint8_t num_fats;
  uint32_t total_sectors;
  uint32_t fat_size;
  uint32_t root_cluster;
  uint16_t fs_info_sector;

  uint64_t fat_start_lba;
  uint64_t data_start_lba;
  uint32_t total_clusters;
  uint32_t bytes_per_cluster;

  uint8_t *fat_cache;
  uint64_t fat_cache_lba;

  char volume_label[12];
  char oem_name[9];
} Fat32Volume;

typedef void (*Fat32DirCallback)(const Fat32DirEntry *entry, void *context);

int fat32_mount(NvmeController *ctrl, uint32_t nsid, const GptPartition *partition, Fat32Volume *volume);
void fat32_unmount(Fat32Volume *volume);

int fat32_stat(Fat32Volume *volume, const char *path, Fat32DirEntry *out_entry);
int fat32_list_dir(Fat32Volume *volume, const char *path, Fat32DirCallback callback, void *context);
int fat32_read(Fat32Volume *volume, const char *path, uint64_t offset, size_t size, void *buf, size_t *bytes_read);
int fat32_read_file(Fat32Volume *volume, const char *path, void **out_buf, size_t *out_size);

int fs_init(void);
int fs_stat(const char *path, Fat32DirEntry *out_entry);
int fs_list_dir(const char *path, Fat32DirCallback callback, void *context);
int fs_read(const char *path, uint64_t offset, size_t size, void *buf, size_t *bytes_read);
int fs_read_file(const char *path, void **out_buf, size_t *out_size);
Fat32Volume *fs_get_root_volume(void);
