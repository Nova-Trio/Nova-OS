#pragma once
#include <stdint.h>
#include <stddef.h>
#include <bootinfo.h>

#define PAGE_SIZE 4096ULL

void pmm_init(BootInfo *boot_info);

void *pmm_alloc_frame(void);
void *pmm_alloc_frames(size_t count);
void *pmm_alloc_aligned_frames(size_t count, size_t align_frames);

void pmm_free_frame(void *phys_addr);
void pmm_free_frames(void *phys_addr, size_t count);

uint64_t pmm_get_total_frames(void);
uint64_t pmm_get_free_frames(void);
uint64_t pmm_get_used_frames(void);
