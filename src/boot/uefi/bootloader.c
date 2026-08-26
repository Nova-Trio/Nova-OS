#include <efi.h>
#include <elf.h>
#include <bootinfo.h>

#define EFI_WHITE_FG 0x0F
#define EFI_BLACK_BG 0x00

#define PTE_PRESENT (1ULL << 0)
#define PTE_WRITABLE (1ULL << 1)
#define PTE_HUGE (1ULL << 7)
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL


typedef struct {
  uint64_t virt_addr;
  uint64_t phys_addr;
  uint64_t num_pages;
  uint32_t flags;
} KernelSegmentMapping;

static void mem_zero(void *dst, UINTN size) {
  uint8_t *d = (uint8_t *)dst;
  for (UINTN i = 0; i < size; i++) {
    d[i] = 0;
  }
}

static void mem_copy(void *dst, const void *src, UINTN size) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (UINTN i = 0; i < size; i++) {
    d[i] = s[i];
  }
}

static EFI_STATUS set_gop_resolution(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
                                     UINTN width, UINTN height) {
  if (!gop) return (EFI_STATUS)-1;

  for (UINTN mode = 0; mode < gop->Mode->MaxMode; mode++) {
    UINTN size;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
    EFI_STATUS s = gop->QueryMode(gop, mode, &size, &info);

    if (info->HorizontalResolution == width && info->VerticalResolution == height) {
      if (info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor ||
        info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
        return gop->SetMode(gop, mode);
        }
    }
  }
  return (EFI_STATUS)-1;
}

static uint64_t *get_or_create_table(uint64_t *entry, EFI_BOOT_SERVICES *bs) {
  if (*entry & PTE_PRESENT) {
    return (uint64_t *)(*entry & PTE_ADDR_MASK);
  }
  EFI_PHYSICAL_ADDRESS table_phys = 0;
  EFI_STATUS s = bs->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &table_phys);
  if (s != EFI_SUCCESS) {
    return NULL;
  }
  mem_zero((void *)table_phys, 4096);
  *entry = table_phys | PTE_PRESENT | PTE_WRITABLE;
  return (uint64_t *)table_phys;
}

static EFI_STATUS map_page(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags, EFI_BOOT_SERVICES *bs) {
  UINTN pml4_idx = (virt >> 39) & 0x1FF;
  UINTN pdpt_idx = (virt >> 30) & 0x1FF;
  UINTN pd_idx = (virt >> 21) & 0x1FF;
  UINTN pt_idx = (virt >> 12) & 0x1FF;

  uint64_t *pdpt = get_or_create_table(&pml4[pml4_idx], bs);
  if (!pdpt) return 1;

  uint64_t *pd = get_or_create_table(&pdpt[pdpt_idx], bs);
  if (!pd) return 1;

  uint64_t *pt = get_or_create_table(&pd[pd_idx], bs);
  if (!pt) return 1;

  pt[pt_idx] = (phys & PTE_ADDR_MASK) | PTE_PRESENT | flags;
  return EFI_SUCCESS;
}

static uint64_t get_max_physical_address(EFI_BOOT_SERVICES *bs) {
  UINTN map_size = 0, map_key = 0, desc_size = 0;
  uint32_t desc_ver = 0;
  bs->GetMemoryMap(&map_size, NULL, &map_key, &desc_size, &desc_ver);
  map_size += 4 * desc_size;

  VOID *mmap = NULL;
  EFI_STATUS s = bs->AllocatePool(EfiLoaderData, map_size, &mmap);
  if (s != EFI_SUCCESS) return 0x100000000ULL;

  s = bs->GetMemoryMap(&map_size, mmap, &map_key, &desc_size, &desc_ver);
  if (s != EFI_SUCCESS) {
    bs->FreePool(mmap);
    return 0x100000000ULL;
  }

  uint64_t max_addr = 0;
  UINTN num_entries = map_size / desc_size;
  for (UINTN i = 0; i < num_entries; i++) {
    EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)((uint8_t *)mmap + (i * desc_size));
    uint64_t end = desc->PhysicalStart + (desc->NumberOfPages * 4096ULL);
    if (end > max_addr) {
      max_addr = end;
    }
  }
  bs->FreePool(mmap);
  return max_addr;
}

static EFI_STATUS map_range_2mb(uint64_t *pml4, uint64_t virt_base, uint64_t phys_base, uint64_t size, EFI_BOOT_SERVICES *bs) {
  uint64_t page_count = (size + 0x1FFFFFULL) / 0x200000ULL;

  for (uint64_t i = 0; i < page_count; i++) {
    uint64_t virt = virt_base + (i * 0x200000ULL);
    uint64_t phys = phys_base + (i * 0x200000ULL);

    UINTN pml4_idx = (virt >> 39) & 0x1FF;
    UINTN pdpt_idx = (virt >> 30) & 0x1FF;
    UINTN pd_idx = (virt >> 21) & 0x1FF;

    uint64_t *pdpt = get_or_create_table(&pml4[pml4_idx], bs);
    if (!pdpt) return 1;

    uint64_t *pd = get_or_create_table(&pdpt[pdpt_idx], bs);
    if (!pd) return 1;

    pd[pd_idx] = (phys & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITABLE | PTE_HUGE;
  }
  return EFI_SUCCESS;
}

static int guid_eq(const EFI_GUID *a, const EFI_GUID *b) {
  if (a->Data1 != b->Data1 || a->Data2 != b->Data2 || a->Data3 != b->Data3) {
    return 0;
  }
  for (int i = 0; i < 8; i++) {
    if (a->Data4[i] != b->Data4[i]) {
      return 0;
    }
  }
  return 1;
}

static void *get_rsdp(EFI_SYSTEM_TABLE *st) {
  static EFI_GUID acpi2_guid = EFI_ACPI_20_TABLE_GUID;
  static EFI_GUID acpi1_guid = EFI_ACPI_10_TABLE_GUID;

  void *rsdp = NULL;
  for (UINTN i = 0; i < st->NumberOfTableEntries; i++) {
    EFI_GUID *g = &st->ConfigurationTable[i].VendorGuid;
    if (guid_eq(g, &acpi2_guid)) {
      return st->ConfigurationTable[i].VendorTable; // XSDT
    }
    if (guid_eq(g, &acpi1_guid)) {
      rsdp = st->ConfigurationTable[i].VendorTable; // RSDT
    }
  }
  return rsdp;
}



EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
  static EFI_GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
  static EFI_GUID sfs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
  static EFI_GUID file_info_guid = EFI_FILE_INFO_ID;

  SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
  SystemTable->ConOut->SetAttribute(SystemTable->ConOut, (EFI_WHITE_FG | EFI_BLACK_BG << 4));
  SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)u"Hello, Cros!\r\n");

  EFI_LOADED_IMAGE_PROTOCOL* loaded_image = NULL;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* sfs = NULL;

  EFI_STATUS s = SystemTable->BootServices->HandleProtocol(ImageHandle, &loaded_image_guid, (VOID**)&loaded_image);
  s = SystemTable->BootServices->HandleProtocol(loaded_image->DeviceHandle, &sfs_guid, (VOID**)&sfs);

  EFI_FILE_PROTOCOL* root_dir = NULL;
  EFI_FILE_PROTOCOL* kernel_file = NULL;

  s = sfs->OpenVolume(sfs, &root_dir);

  root_dir->Open(root_dir, &kernel_file, (CHAR16 *)u"\\EFI\\novaos\\kernel.elf", EFI_FILE_MODE_READ, 0);

  UINTN info_size = 0;
  kernel_file->GetInfo(kernel_file, &file_info_guid, &info_size, NULL);

  EFI_FILE_INFO* file_info = NULL;
  SystemTable->BootServices->AllocatePool(EfiLoaderData, info_size, (VOID**)&file_info);
  kernel_file->GetInfo(kernel_file, &file_info_guid, &info_size, (VOID*)file_info);

  UINTN kernel_size = file_info->FileSize;
  SystemTable->BootServices->FreePool(file_info);

  VOID *kernel_buffer = NULL;
  SystemTable->BootServices->AllocatePool(EfiLoaderData, kernel_size, &kernel_buffer);

  s = kernel_file->Read(kernel_file, &kernel_size, kernel_buffer);

  unsigned char* kernel = (unsigned char*)kernel_buffer;
  Elf64_Ehdr* ehdr = (Elf64_Ehdr*)kernel;

  if(ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' || ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F' ||
    ehdr->e_ident[4] != ELFCLASS64 || ehdr->e_ident[5] != ELFDATA2LSB || ehdr->e_machine != EM_X86_64 ||
    ehdr->e_type != ET_EXEC
  ){
    SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)u"Kernel corrupted.\r\n");
    kernel_file->Close(kernel_file);
    root_dir->Close(root_dir);
    SystemTable->BootServices->FreePool(kernel_buffer);
    return 1; // EFI_LOAD_ERROR
  }

  uint64_t k_min_virt = (uint64_t)-1;
  uint64_t k_max_virt = 0;
  UINTN loadable_segments = 0;

  for (UINTN i = 0; i < ehdr->e_phnum; i++) {
    Elf64_Phdr *phdr = (Elf64_Phdr *)((uint8_t *)kernel_buffer + ehdr->e_phoff + (i * ehdr->e_phentsize));
    if (phdr->p_type != PT_LOAD) {
      continue;
    }

    if (phdr->p_offset + phdr->p_filesz > kernel_size || phdr->p_filesz > phdr->p_memsz) {
      SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)u"Malformed segment detected\r\n");
      SystemTable->BootServices->FreePool(kernel_buffer);
      return 1;
    }

    if (phdr->p_vaddr < k_min_virt) {
      k_min_virt = phdr->p_vaddr;
    }
    if (phdr->p_vaddr + phdr->p_memsz > k_max_virt) {
      k_max_virt = phdr->p_vaddr + phdr->p_memsz;
    }
    loadable_segments++;
  }

  if (loadable_segments == 0) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)u"No PT_LOAD segments found\r\n");
    SystemTable->BootServices->FreePool(kernel_buffer);
    return 1;
  }

  uint64_t aligned_min_virt = k_min_virt & ~0xFFFULL;
  uint64_t aligned_max_virt = (k_max_virt + 0xFFFULL) & ~0xFFFULL;
  UINTN total_kernel_pages = (aligned_max_virt - aligned_min_virt) / 4096ULL;
  uint64_t total_kernel_size = total_kernel_pages * 4096ULL;

  EFI_PHYSICAL_ADDRESS kernel_phys_base = 0;
  s = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, total_kernel_pages, &kernel_phys_base);
  if (s != EFI_SUCCESS) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)u"Failed to allocate physical pages\r\n");
    SystemTable->BootServices->FreePool(kernel_buffer);
    return s;
  }
  mem_zero((VOID *)kernel_phys_base, total_kernel_size);

  KernelSegmentMapping *mappings = NULL;
  s = SystemTable->BootServices->AllocatePool(EfiLoaderData, loadable_segments * sizeof(KernelSegmentMapping), (VOID **)&mappings);
  if (s != EFI_SUCCESS) {
    SystemTable->BootServices->FreePages(kernel_phys_base, total_kernel_pages);
    SystemTable->BootServices->FreePool(kernel_buffer);
    return s;
  }

  UINTN seg_idx = 0;
  for (UINTN i = 0; i < ehdr->e_phnum; i++) {
    Elf64_Phdr *phdr = (Elf64_Phdr *)((uint8_t *)kernel_buffer + ehdr->e_phoff + (i * ehdr->e_phentsize));
    if (phdr->p_type != PT_LOAD) {
      continue;
    }

    uint64_t page_offset = phdr->p_vaddr & 0xFFFULL;
    uint64_t aligned_vaddr = phdr->p_vaddr - page_offset;
    UINTN pages = (phdr->p_memsz + page_offset + 0xFFFULL) / 4096ULL;
    uint64_t seg_phys = kernel_phys_base + (aligned_vaddr - aligned_min_virt);

    mem_copy((VOID *)(seg_phys + page_offset), (const VOID *)((uint8_t *)kernel_buffer + phdr->p_offset), phdr->p_filesz);

    mappings[seg_idx].virt_addr = aligned_vaddr;
    mappings[seg_idx].phys_addr = seg_phys;
    mappings[seg_idx].num_pages = pages;
    mappings[seg_idx].flags = phdr->p_flags;
    seg_idx++;
  }

  uint64_t kernel_entry = ehdr->e_entry;
  SystemTable->BootServices->FreePool(kernel_buffer);

  SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)u"Kernel loaded into physical memory.\r\n");

  // When the kernel gets disk & fs drivers remove this

  EFI_FILE_PROTOCOL *font_file = NULL;
  s = root_dir->Open(root_dir, &font_file, (CHAR16 *)u"\\EFI\\novaos\\zap-light16.psf", EFI_FILE_MODE_READ, 0);
  if (s != EFI_SUCCESS) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)u"Failed to open font file\r\n");
    return s;
  }

  UINTN font_info_size = 0;
  font_file->GetInfo(font_file, &file_info_guid, &font_info_size, NULL);

  EFI_FILE_INFO *font_file_info = NULL;
  SystemTable->BootServices->AllocatePool(EfiLoaderData, font_info_size, (VOID **)&font_file_info);
  font_file->GetInfo(font_file, &file_info_guid, &font_info_size, (VOID *)font_file_info);

  UINTN font_size = font_file_info->FileSize;
  SystemTable->BootServices->FreePool(font_file_info);

  UINTN font_pages = (font_size + 0xFFFULL) / 4096ULL;
  EFI_PHYSICAL_ADDRESS font_phys = 0;
  s = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, font_pages, &font_phys);
  if (s != EFI_SUCCESS) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)u"Failed to allocate pages for font\r\n");
    return s;
  }

  s = font_file->Read(font_file, &font_size, (VOID *)font_phys);
  font_file->Close(font_file);
  root_dir->Close(root_dir);
  if (s != EFI_SUCCESS) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)u"Failed to read font file\r\n");
    return s;
  }

  PSF1_Header *font_hdr = (PSF1_Header *)font_phys;
  if (font_hdr->magic[0] != PSF1_MAGIC0 || font_hdr->magic[1] != PSF1_MAGIC1) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)u"Invalid PSF1 font\r\n");
    return 1;
  }

  // END

  static EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;

  s = SystemTable->BootServices->LocateProtocol(&gop_guid, NULL, (VOID **)&gop);
  if (s != EFI_SUCCESS) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)u"Failed to locate GOP\r\n");
    return s;
  }

  s = set_gop_resolution(gop, 1920, 1080);

  BootFramebuffer fb = {
    .base = gop->Mode->FrameBufferBase,
    .size = gop->Mode->FrameBufferSize,
    .width = gop->Mode->Info->HorizontalResolution,
    .height = gop->Mode->Info->VerticalResolution,
    .pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine,
    .format = (BootPixelFormat)gop->Mode->Info->PixelFormat
  };

  uint64_t max_phys = get_max_physical_address(SystemTable->BootServices);
  if (fb.base + fb.size > max_phys) {
    max_phys = fb.base + fb.size;
  }
  if (max_phys < 0x100000000ULL) {
    max_phys = 0x100000000ULL;
  }

  EFI_PHYSICAL_ADDRESS pml4_phys = 0;
  s = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &pml4_phys);
  if (s != EFI_SUCCESS) return s;
  mem_zero((VOID *)pml4_phys, 4096);
  uint64_t *pml4 = (uint64_t *)pml4_phys;

  s = map_range_2mb(pml4, 0, 0, max_phys, SystemTable->BootServices);
  if (s != EFI_SUCCESS) return s;

  s = map_range_2mb(pml4, HHDM_BASE, 0, max_phys, SystemTable->BootServices);
  if (s != EFI_SUCCESS) return s;

  for (UINTN i = 0; i < loadable_segments; i++) {
    uint64_t pte_flags = (mappings[i].flags & PF_W) ? PTE_WRITABLE : 0;
    for (UINTN p = 0; p < mappings[i].num_pages; p++) {
      uint64_t v = mappings[i].virt_addr + (p * 4096ULL);
      uint64_t ph = mappings[i].phys_addr + (p * 4096ULL);
      s = map_page(pml4, v, ph, pte_flags, SystemTable->BootServices);
      if (s != EFI_SUCCESS) return s;
    }
  }
  SystemTable->BootServices->FreePool(mappings);



  #define STACK_PAGES 8
  #define STACK_VADDR 0xFFFFFFFF80100000ULL

  EFI_PHYSICAL_ADDRESS stack_phys = 0;
  s = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, STACK_PAGES, &stack_phys);
  if (s != EFI_SUCCESS) return s;
  mem_zero((VOID *)stack_phys, STACK_PAGES * 4096ULL);

  for (UINTN p = 0; p < STACK_PAGES; p++) {
    uint64_t v = STACK_VADDR + (p * 4096ULL);
    uint64_t ph = stack_phys + (p * 4096ULL);
    s = map_page(pml4, v, ph, PTE_WRITABLE, SystemTable->BootServices);
    if (s != EFI_SUCCESS) return s;
  }
  uint64_t stack_top = STACK_VADDR + (STACK_PAGES * 4096ULL);


  VOID *rsdp = get_rsdp(SystemTable);

  EFI_PHYSICAL_ADDRESS boot_info_phys = 0;
  s = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &boot_info_phys);
  if (s != EFI_SUCCESS) return s;
  mem_zero((VOID *)boot_info_phys, 4096);

  BootInfo *boot_info = (BootInfo *)boot_info_phys;
  boot_info->framebuffer = fb;
  boot_info->framebuffer.base += HHDM_BASE;
  boot_info->rsdp = rsdp ? (VOID *)((uint64_t)rsdp + HHDM_BASE) : NULL;
  boot_info->pml4 = (uint64_t *)(pml4_phys + HHDM_BASE);

  // Read above
  boot_info->font.header = (PSF1_Header *)(font_phys + HHDM_BASE);
  boot_info->font.glyph_buffer = (void *)(font_phys + sizeof(PSF1_Header) + HHDM_BASE);
  // END

  boot_info->kernel.phys_base = kernel_phys_base;
  boot_info->kernel.virt_base = aligned_min_virt;
  boot_info->kernel.size = total_kernel_size;

  boot_info->stack.phys_base = stack_phys;
  boot_info->stack.virt_base = STACK_VADDR;
  boot_info->stack.size = STACK_PAGES * 4096ULL;

  UINTN map_size = 0;
  UINTN map_key = 0;
  UINTN desc_size = 0;
  uint32_t desc_ver = 0;
  VOID *mmap = NULL;

  SystemTable->BootServices->GetMemoryMap(&map_size, NULL, &map_key, &desc_size, &desc_ver);
  map_size += 16 * desc_size;

  s = SystemTable->BootServices->AllocatePool(EfiLoaderData, map_size, &mmap);
  if (s != EFI_SUCCESS) return s;

  for (;;) {
    UINTN actual_size = map_size;
    s = SystemTable->BootServices->GetMemoryMap(&actual_size, mmap, &map_key, &desc_size, &desc_ver);
    if (s != EFI_SUCCESS) {
      SystemTable->BootServices->FreePool(mmap);
      map_size = 0;
      SystemTable->BootServices->GetMemoryMap(&map_size, NULL, &map_key, &desc_size, &desc_ver);
      map_size += 16 * desc_size;
      s = SystemTable->BootServices->AllocatePool(EfiLoaderData, map_size, &mmap);
      if (s != EFI_SUCCESS) return s;
      continue;
    }

    boot_info->memory_map.map = (VOID *)((uint64_t)mmap + HHDM_BASE);
    boot_info->memory_map.size = actual_size;
    boot_info->memory_map.descriptor_size = desc_size;
    boot_info->memory_map.descriptor_version = desc_ver;

    s = SystemTable->BootServices->ExitBootServices(ImageHandle, map_key);
    if (s == EFI_SUCCESS) {
      break;
    }
  }

  uint64_t boot_info_virt = boot_info_phys + HHDM_BASE;

  __asm__ volatile(
    "cli\n\t"
    "mov %0, %%cr3\n\t"
    "mov %1, %%rsp\n\t"
    "mov %1, %%rbp\n\t"
    "mov %2, %%rdi\n\t"
    "jmp *%3"
    :
    : "r"(pml4_phys),
      "r"(stack_top),
      "r"(boot_info_virt),
      "r"(kernel_entry)
    : "memory"
  );

  return EFI_SUCCESS;
}
