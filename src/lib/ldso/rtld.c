#include "rtld.h"

#define USER_LIB_BASE 0x0000780000000000ULL

#define RTLD_HEAP_BASE 0x0000700080000000ULL
#define RTLD_LIB_BASE  0x0000780000000000ULL

static SharedObject *gObjects = NULL;
static SharedObject *gMainObj = NULL;
static SharedObject gLdsoObj;
static uint64_t gPageSize = 4096;
static int gIsStartingUp = 1;
static uint64_t gRtldHeapNext = RTLD_HEAP_BASE;
static uint64_t gNextLibAddr  = RTLD_LIB_BASE;

static SharedObject **gInitOrder = NULL;
static size_t gInitOrderCount = 0;
static size_t gInitOrderCapacity = 0;

static const Elf64_Sym *rtldLookupInObject(const SharedObject *obj, const char *name);
static uint64_t rtldResolveSymbol(const char *name, const SharedObject **outObj, const Elf64_Sym **outSym);

static void rtldAddDependency(SharedObject *obj, SharedObject *dep) {
  if (!obj || !dep || obj == dep) {
    return;
  }
  for (size_t i = 0; i < obj->depCount; i++) {
    if (obj->deps[i] == dep) {
      return;
    }
  }
  if (obj->depCount >= obj->depCapacity) {
    size_t newCap = obj->depCapacity == 0 ? 8 : obj->depCapacity * 2;
    SharedObject **newDeps = (SharedObject **)rtldAlloc(newCap * sizeof(SharedObject *));
    if (!newDeps) {
      return;
    }
    for (size_t i = 0; i < obj->depCount; i++) {
      newDeps[i] = obj->deps[i];
    }
    obj->deps = newDeps;
    obj->depCapacity = newCap;
  }
  obj->deps[obj->depCount++] = dep;
}

static void rtldAppendInitOrder(SharedObject *obj) {
  if (gInitOrderCount >= gInitOrderCapacity) {
    size_t newCap = gInitOrderCapacity == 0 ? 16 : gInitOrderCapacity * 2;
    SharedObject **newOrder = (SharedObject **)rtldAlloc(newCap * sizeof(SharedObject *));
    if (!newOrder) {
      return;
    }
    for (size_t i = 0; i < gInitOrderCount; i++) {
      newOrder[i] = gInitOrder[i];
    }
    gInitOrder = newOrder;
    gInitOrderCapacity = newCap;
  }
  gInitOrder[gInitOrderCount++] = obj;
}

static void rtldExecuteFini(SharedObject *obj) {
  if (!obj || obj->finiCalled) {
    return;
  }
  obj->finiCalled = 1;

  if (obj->finiArray && obj->finiArraySz > 0 && obj->finiArraySz < 4096) {
    uint64_t arrayAddr = (uint64_t)obj->finiArray;
    if (arrayAddr >= obj->base && arrayAddr < (obj->base + obj->totalSpan)) {
      for (size_t i = obj->finiArraySz; i > 0; i--) {
        void (*func)(void) = obj->finiArray[i - 1];
        if (func && (uint64_t)func >= obj->base && (uint64_t)func < (obj->base + obj->totalSpan)) {
          func();
        }
      }
    }
  }
  if (obj->fini) {
    uint64_t funcAddr = (uint64_t)obj->fini;
    if (funcAddr >= obj->base && funcAddr < (obj->base + obj->totalSpan)) {
      obj->fini();
    }
  }
}

static char gDlErrorBuf[128];
static int gHasDlError = 0;

static void setDlError(const char *msg) {
  size_t len = rtldStrlen(msg);
  if (len >= sizeof(gDlErrorBuf)) {
    len = sizeof(gDlErrorBuf) - 1;
  }
  rtldMemcpy(gDlErrorBuf, msg, len);
  gDlErrorBuf[len] = '\0';
  gHasDlError = 1;
}

char *rtldDlError(void) {
  if (!gHasDlError) {
    return NULL;
  }
  gHasDlError = 0;
  return gDlErrorBuf;
}

void *rtldDlOpen(const char *filename, int flags) {
  (void)flags;
  if (!filename) {
    return (void *)gMainObj;
  }

  for (SharedObject *cur = gObjects; cur; cur = cur->next) {
    if (rtldStrcmp(cur->name, filename) == 0) {
      cur->refCount++;
      return (void *)cur;
    }
  }

  SharedObject *obj = rtldLoadObject(filename);
  if (!obj) {
    setDlError("Failed to load shared object");
    return NULL;
  }

  obj->refCount++;
  rtldRelocateAll();
  rtldCallInit();

  return (void *)obj;
}

void *rtldDlSym(void *handle, const char *symbol) {
  if (!symbol) {
    setDlError("Invalid symbol name");
    return NULL;
  }

  if (!handle) {
    uint64_t val = rtldResolveSymbol(symbol, NULL, NULL);
    if (!val) {
      setDlError("Symbol not found");
      return NULL;
    }
    return (void *)val;
  }

  SharedObject *obj = (SharedObject *)handle;
  const Elf64_Sym *sym = rtldLookupInObject(obj, symbol);
  if (sym) {
    return (void *)(obj->base + sym->st_value);
  }

  if (obj->dynamic && obj->strtab) {
    for (const Elf64_Dyn *dyn = obj->dynamic; dyn->d_tag != DT_NULL; dyn++) {
      if (dyn->d_tag == DT_NEEDED) {
        const char *depName = obj->strtab + dyn->d_un.d_val;
        for (SharedObject *cur = gObjects; cur; cur = cur->next) {
          if (rtldStrcmp(cur->name, depName) == 0) {
            const Elf64_Sym *dsym = rtldLookupInObject(cur, symbol);
            if (dsym) {
              return (void *)(cur->base + dsym->st_value);
            }
          }
        }
      }
    }
  }

  setDlError("Symbol not found in specified object");
  return NULL;
}

int rtldDlClose(void *handle) {
  if (!handle || handle == (void *)gMainObj || handle == (void *)&gLdsoObj) {
    return 0;
  }

  SharedObject *obj = (SharedObject *)handle;
  if (obj->isPinned) {
    return 0;
  }

  if (obj->refCount > 1) {
    obj->refCount--;
    return 0;
  }

  obj->refCount = 0;
  rtldExecuteFini(obj);

  for (size_t i = 0; i < obj->depCount; i++) {
    rtldDlClose(obj->deps[i]);
  }

  SharedObject **curr = &gObjects;
  while (*curr) {
    if (*curr == obj) {
      *curr = obj->next;
      break;
    }
    curr = &(*curr)->next;
  }

  for (size_t i = 0; i < gInitOrderCount; i++) {
    if (gInitOrder[i] == obj) {
      gInitOrder[i] = NULL;
      break;
    }
  }

  if (obj->mapBase && obj->totalSpan) {
    munmap((void *)obj->mapBase, obj->totalSpan);
  }

  return 0;
}

__attribute__((visibility("default"))) void rtldCallFini(void);

__attribute__((visibility("default"))) void *dlopen(const char *filename, int flags) {
  return rtldDlOpen(filename, flags);
}

__attribute__((visibility("default"))) void *dlsym(void *handle, const char *symbol) {
  return rtldDlSym(handle, symbol);
}

__attribute__((visibility("default"))) int dlclose(void *handle) {
  return rtldDlClose(handle);
}

__attribute__((visibility("default"))) char *dlerror(void) {
  return rtldDlError();
}

__attribute__((visibility("default"))) void rtldCallFini(void) {
  for (size_t i = gInitOrderCount; i > 0; i--) {
    SharedObject *obj = gInitOrder[i - 1];
    if (obj) {
      rtldExecuteFini(obj);
    }
  }
}

void *rtldAlloc(size_t size) {
  size = (size + 15) & ~15ULL;
  static uint8_t *heapCurr = NULL;
  static size_t heapLeft = 0;

  if (heapLeft < size) {
    size_t chunkSize = (size + 4095) & ~4095ULL;
    if (chunkSize < 65536) {
      chunkSize = 65536;
    }
    void *p = mmap((void *)gRtldHeapNext, chunkSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if ((int64_t)p < 0 || !p) {
      return NULL;
    }
    gRtldHeapNext += chunkSize;
    heapCurr = (uint8_t *)p;
    heapLeft = chunkSize;
  }

  void *res = heapCurr;
  heapCurr += size;
  heapLeft -= size;
  return res;
}

size_t rtldStrlen(const char *s) {
  size_t len = 0;
  while (s && s[len]) len++;
  return len;
}

int rtldStrcmp(const char *s1, const char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int rtldStrncmp(const char *s1, const char *s2, size_t n) {
  while (n && *s1 && (*s1 == *s2)) {
    s1++;
    s2++;
    n--;
  }
  return n ? (*(const unsigned char *)s1 - *(const unsigned char *)s2) : 0;
}

void *rtldMemcpy(void *dest, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dest;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < n; i++) {
    d[i] = s[i];
  }
  return dest;
}

void *rtldMemset(void *s, int c, size_t n) {
  uint8_t *p = (uint8_t *)s;
  for (size_t i = 0; i < n; i++) {
    p[i] = (uint8_t)c;
  }
  return s;
}

char *rtldStrdup(const char *s) {
  size_t len = rtldStrlen(s);
  char *d = (char *)rtldAlloc(len + 1);
  if (!d) return NULL;
  rtldMemcpy(d, s, len + 1);
  return d;
}

uint32_t rtldElfHash(const char *name) {
  uint32_t h = 0, g;
  while (*name) {
    h = (h << 4) + (uint8_t)*name++;
    if ((g = h & 0xf0000000)) {
      h ^= g >> 24;
    }
    h &= ~g;
  }
  return h;
}

uint32_t rtldGnuHash(const char *name) {
  uint32_t h = 5381;
  for (unsigned char c = *(const unsigned char *)name; c; c = *(const unsigned char *)++name) {
    h = (h << 5) + h + c;
  }
  return h;
}

static void rtldError(const char *msg) {
  write(2, msg, rtldStrlen(msg));
  exit(127);
}

static void rtldParseDynamic(SharedObject *obj) {
  if (!obj->dynamic) return;

  for (const Elf64_Dyn *dyn = obj->dynamic; dyn->d_tag != DT_NULL; dyn++) {
    switch (dyn->d_tag) {
      case DT_STRTAB:
        obj->strtab = (const char *)(obj->base + dyn->d_un.d_ptr);
        break;
      case DT_SYMTAB:
        obj->symtab = (const Elf64_Sym *)(obj->base + dyn->d_un.d_ptr);
        break;
      case DT_RELA:
        obj->rela = (const Elf64_Rela *)(obj->base + dyn->d_un.d_ptr);
        break;
      case DT_RELASZ:
        obj->relasz = dyn->d_un.d_val;
        break;
      case DT_JMPREL:
        obj->jmprel = (const Elf64_Rela *)(obj->base + dyn->d_un.d_ptr);
        break;
      case DT_PLTRELSZ:
        obj->pltrelsz = dyn->d_un.d_val;
        break;
      case DT_HASH:
        obj->hash = (const uint32_t *)(obj->base + dyn->d_un.d_ptr);
        break;
      case DT_GNU_HASH:
        obj->gnuHash = (const uint32_t *)(obj->base + dyn->d_un.d_ptr);
        break;
      case DT_INIT:
        obj->init = (void (*)(void))(obj->base + dyn->d_un.d_ptr);
        break;
      case DT_INIT_ARRAY:
        obj->initArray = (void (**)(void))(obj->base + dyn->d_un.d_ptr);
        break;
      case DT_INIT_ARRAYSZ:
        obj->initArraySz = dyn->d_un.d_val / sizeof(void *);
        break;
      case DT_FINI:
        obj->fini = (void (*)(void))(obj->base + dyn->d_un.d_ptr);
        break;
      case DT_FINI_ARRAY:
        obj->finiArray = (void (**)(void))(obj->base + dyn->d_un.d_ptr);
        break;
      case DT_FINI_ARRAYSZ:
        obj->finiArraySz = dyn->d_un.d_val / sizeof(void *);
        break;
    }
  }
}

static const Elf64_Sym *rtldLookupInObject(const SharedObject *obj, const char *name) {
  if (!obj->symtab || !obj->strtab) return NULL;

  if (obj->gnuHash) {
    const uint32_t *h = obj->gnuHash;
    uint32_t nbuckets = h[0];
    uint32_t symOffset = h[1];
    uint32_t bloomSize = h[2];
    uint32_t bloomShift = h[3];
    const uint64_t *bloom = (const uint64_t *)&h[4];
    const uint32_t *buckets = (const uint32_t *)&bloom[bloomSize];
    const uint32_t *chains = &buckets[nbuckets];

    uint32_t hash = rtldGnuHash(name);
    uint64_t word = bloom[(hash / 64) % bloomSize];
    uint64_t mask = (1ULL << (hash % 64)) | (1ULL << ((hash >> bloomShift) % 64));

    if ((word & mask) == mask) {
      uint32_t symIdx = buckets[hash % nbuckets];
      if (symIdx >= symOffset) {
        for (;; symIdx++) {
          uint32_t chainVal = chains[symIdx - symOffset];
          if ((chainVal >> 1) == (hash >> 1)) {
            const Elf64_Sym *sym = &obj->symtab[symIdx];
            if (rtldStrcmp(obj->strtab + sym->st_name, name) == 0) {
              if (sym->st_shndx != SHN_UNDEF) {
                return sym;
              }
            }
          }
          if (chainVal & 1) break;
        }
      }
    }
  }

  if (obj->hash) {
    const uint32_t *h = obj->hash;
    uint32_t nbucket = h[0];
    const uint32_t *buckets = &h[2];
    const uint32_t *chains = &buckets[nbucket];

    uint32_t hash = rtldElfHash(name);
    for (uint32_t symIdx = buckets[hash % nbucket]; symIdx != 0; symIdx = chains[symIdx]) {
      const Elf64_Sym *sym = &obj->symtab[symIdx];
      if (rtldStrcmp(obj->strtab + sym->st_name, name) == 0) {
        if (sym->st_shndx != SHN_UNDEF) {
          return sym;
        }
      }
    }
  }

  return NULL;
}

static uint64_t rtldResolveSymbol(const char *name, const SharedObject **outObj, const Elf64_Sym **outSym) {
  const Elf64_Sym *weakSym = NULL;
  const SharedObject *weakObj = NULL;

  if (gMainObj) {
    const Elf64_Sym *sym = rtldLookupInObject(gMainObj, name);
    if (sym) {
      uint8_t bind = ELF64_ST_BIND(sym->st_info);
      if (bind == STB_GLOBAL) {
        if (outObj) *outObj = gMainObj;
        if (outSym) *outSym = sym;
        return gMainObj->base + sym->st_value;
      } else if (bind == STB_WEAK && !weakSym) {
        weakSym = sym;
        weakObj = gMainObj;
      }
    }
  }

  for (SharedObject *obj = gObjects; obj; obj = obj->next) {
    const Elf64_Sym *sym = rtldLookupInObject(obj, name);
    if (sym) {
      uint8_t bind = ELF64_ST_BIND(sym->st_info);
      if (bind == STB_GLOBAL) {
        if (outObj) *outObj = obj;
        if (outSym) *outSym = sym;
        return obj->base + sym->st_value;
      } else if (bind == STB_WEAK && !weakSym) {
        weakSym = sym;
        weakObj = obj;
      }
    }
  }

  if (weakSym && weakObj) {
    if (outObj) *outObj = weakObj;
    if (outSym) *outSym = weakSym;
    return weakObj->base + weakSym->st_value;
  }

  return 0;
}

SharedObject *rtldLoadObject(const char *name) {
  if (gMainObj && rtldStrcmp(gMainObj->name, name) == 0) {
    return gMainObj;
  }

  for (SharedObject *cur = gObjects; cur; cur = cur->next) {
    if (rtldStrcmp(cur->name, name) == 0) {
      return cur;
    }
  }

  int fd = -1;
  char pathBuf[256];

  if (name[0] == '/' || (name[0] == '.' && name[1] == '/')) {
    fd = open(name, O_RDONLY);
  } else {
    static const char *searchDirs[] = { "/lib/", "/bin/", "/EFI/novaos/", "/nova/drivers/", NULL };
    for (size_t i = 0; searchDirs[i]; i++) {
      size_t dlen = rtldStrlen(searchDirs[i]);
      size_t nlen = rtldStrlen(name);
      if (dlen + nlen < sizeof(pathBuf)) {
        rtldMemcpy(pathBuf, searchDirs[i], dlen);
        rtldMemcpy(pathBuf + dlen, name, nlen + 1);
        fd = open(pathBuf, O_RDONLY);
        if (fd >= 0) break;
      }
    }
  }

  if (fd < 0) {
    write(2, "[ld-crow] Error: Unable to open shared object '", 47);
    write(2, name, rtldStrlen(name));
    write(2, "'\n", 2);
    return NULL;
  }

  Elf64_Ehdr ehdr;
  if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
    close(fd);
    return NULL;
  }

  if (ehdr.e_ident[0] != ELFMAG0 || ehdr.e_ident[1] != ELFMAG1 || ehdr.e_ident[2] != ELFMAG2 || ehdr.e_ident[3] != ELFMAG3 || ehdr.e_type != ET_DYN || ehdr.e_machine != EM_X86_64) {
    close(fd);
    return NULL;
  }

  size_t phdrsSz = (size_t)ehdr.e_phnum * ehdr.e_phentsize;
  Elf64_Phdr *phdrs = (Elf64_Phdr *)rtldAlloc(phdrsSz);
  if (!phdrs) {
    close(fd);
    return NULL;
  }

  lseek(fd, (int64_t)ehdr.e_phoff, SEEK_SET);
  if (read(fd, phdrs, phdrsSz) != (int64_t)phdrsSz) {
    close(fd);
    return NULL;
  }

  uint64_t minVaddr = ~0ULL;
  uint64_t maxVaddr = 0;
  for (size_t i = 0; i < ehdr.e_phnum; i++) {
    if (phdrs[i].p_type == PT_LOAD) {
      if (phdrs[i].p_vaddr < minVaddr) {
        minVaddr = phdrs[i].p_vaddr;
      }
      if (phdrs[i].p_vaddr + phdrs[i].p_memsz > maxVaddr) {
        maxVaddr = phdrs[i].p_vaddr + phdrs[i].p_memsz;
      }
    }
  }

  minVaddr &= ~(gPageSize - 1);
  maxVaddr = (maxVaddr + gPageSize - 1) & ~(gPageSize - 1);
  size_t totalSpan = (size_t)(maxVaddr - minVaddr);

  uint64_t mapBase = gNextLibAddr;
  gNextLibAddr = (mapBase + totalSpan + 0x200000ULL - 1) & ~(0x200000ULL - 1);
  uint64_t loadBias = mapBase - minVaddr;

  for (size_t i = 0; i < ehdr.e_phnum; i++) {
    if (phdrs[i].p_type != PT_LOAD) continue;

    uint64_t segStart = (phdrs[i].p_vaddr + loadBias) & ~(gPageSize - 1);
    uint64_t segEnd = (phdrs[i].p_vaddr + loadBias + phdrs[i].p_memsz + gPageSize - 1) & ~(gPageSize - 1);
    size_t segLen = (size_t)(segEnd - segStart);
    uint64_t fileOffset = phdrs[i].p_offset & ~(gPageSize - 1);

    int prot = 0;
    if (phdrs[i].p_flags & PF_R) prot |= PROT_READ;
    if (phdrs[i].p_flags & PF_W) prot |= PROT_WRITE;
    if (phdrs[i].p_flags & PF_X) prot |= PROT_EXEC;

    void *mapped = mmap((void *)segStart, segLen, prot, MAP_PRIVATE | MAP_FIXED, fd, (int64_t)fileOffset);
    if ((int64_t)mapped < 0) {
      write(2, "[ld-crow] Error: Segment mmap failed\n", 37);
      close(fd);
      return NULL;
    }

    if (phdrs[i].p_memsz > phdrs[i].p_filesz) {
      uint64_t bssStart = phdrs[i].p_vaddr + loadBias + phdrs[i].p_filesz;
      uint64_t bssEnd = phdrs[i].p_vaddr + loadBias + phdrs[i].p_memsz;
      if (prot & PROT_WRITE) {
        rtldMemset((void *)bssStart, 0, (size_t)(bssEnd - bssStart));
      }
    }
  }

  close(fd);

  SharedObject *obj = (SharedObject *)rtldAlloc(sizeof(SharedObject));
  rtldMemset(obj, 0, sizeof(SharedObject));

  size_t nlen = rtldStrlen(name);
  if (nlen >= sizeof(obj->name)) nlen = sizeof(obj->name) - 1;
  rtldMemcpy(obj->name, name, nlen);
  obj->name[nlen] = '\0';

  obj->base = loadBias;
  obj->mapBase = (uint64_t)mapBase;
  obj->totalSpan = totalSpan;
  obj->phdrs = phdrs;
  obj->phnum = ehdr.e_phnum;
  if (gIsStartingUp) {
    obj->isPinned = 1;
  }

  for (size_t i = 0; i < ehdr.e_phnum; i++) {
    if (phdrs[i].p_type == PT_DYNAMIC) {
      obj->dynamic = (const Elf64_Dyn *)(loadBias + phdrs[i].p_vaddr);
      break;
    }
  }

  rtldParseDynamic(obj);

  obj->next = gObjects;
  gObjects = obj;

  if (obj->dynamic && obj->strtab) {
    for (const Elf64_Dyn *dyn = obj->dynamic; dyn->d_tag != DT_NULL; dyn++) {
      if (dyn->d_tag == DT_NEEDED) {
        const char *depName = obj->strtab + dyn->d_un.d_val;
        SharedObject *depObj = rtldLoadObject(depName);
        if (depObj) {
          rtldAddDependency(obj, depObj);
        }
      }
    }
  }

  return obj;
}

static void rtldRelocateObject(SharedObject *obj) {
  if (obj->relocated) return;

  const Elf64_Rela *relas[] = { obj->rela, obj->jmprel };
  size_t sizes[] = { obj->relasz, obj->pltrelsz };

  for (size_t r = 0; r < 2; r++) {
    const Elf64_Rela *rela = relas[r];
    size_t count = sizes[r] / sizeof(Elf64_Rela);
    if (!rela || count == 0) continue;

    for (size_t i = 0; i < count; i++) {
      const Elf64_Rela *rel = &rela[i];
      uint32_t type = (uint32_t)ELF64_R_TYPE(rel->r_info);
      uint32_t symIdx = (uint32_t)ELF64_R_SYM(rel->r_info);
      uint64_t *target = (uint64_t *)(obj->base + rel->r_offset);

      switch (type) {
        case R_X86_64_NONE:
          break;

        case R_X86_64_RELATIVE:
          *target = obj->base + (uint64_t)rel->r_addend;
          break;

        case R_X86_64_64:
        case R_X86_64_GLOB_DAT:
        case R_X86_64_JUMP_SLOT: {
          const Elf64_Sym *sym = &obj->symtab[symIdx];
          const char *sname = obj->strtab + sym->st_name;
          uint64_t val = rtldResolveSymbol(sname, NULL, NULL);
          if (!val && (ELF64_ST_BIND(sym->st_info) != 2)) {
            write(2, "[ld-crow] Error: Unresolved symbol '", 36);
            write(2, sname, rtldStrlen(sname));
            write(2, "'\n", 2);
            rtldError("Relocation failure\n");
          }
          if (type == R_X86_64_64) {
            *target = val + (uint64_t)rel->r_addend;
          } else {
            *target = val;
          }
          break;
        }

        case R_X86_64_COPY: {
          const Elf64_Sym *sym = &obj->symtab[symIdx];
          const char *sname = obj->strtab + sym->st_name;
          const SharedObject *srcObj = NULL;
          const Elf64_Sym *srcSym = NULL;
          uint64_t srcVal = rtldResolveSymbol(sname, &srcObj, &srcSym);
          if (srcVal && srcSym) {
            size_t copyLen = (sym->st_size < srcSym->st_size) ? (size_t)sym->st_size : (size_t)srcSym->st_size;
            rtldMemcpy(target, (const void *)srcVal, copyLen);
          }
          break;
        }

        default:
          break;
      }
    }
  }

  obj->relocated = 1;
}

void rtldRelocateAll(void) {
  for (SharedObject *obj = gObjects; obj; obj = obj->next) {
    rtldRelocateObject(obj);
  }
  if (gMainObj) {
    rtldRelocateObject(gMainObj);
  }
}

static void rtldInitDfs(SharedObject *obj) {
  if (!obj || obj->initCalled || obj->visiting) {
    return;
  }
  obj->visiting = 1;

  for (size_t i = 0; i < obj->depCount; i++) {
    rtldInitDfs(obj->deps[i]);
  }

  obj->visiting = 0;
  obj->initCalled = 1;

  if (obj->init) {
    obj->init();
  }
  if (obj->initArray && obj->initArraySz > 0) {
    for (size_t i = 0; i < obj->initArraySz; i++) {
      if (obj->initArray[i]) {
        obj->initArray[i]();
      }
    }
  }

  rtldAppendInitOrder(obj);
}

void rtldCallInit(void) {
  for (SharedObject *obj = gObjects; obj; obj = obj->next) {
    rtldInitDfs(obj);
  }
  if (gMainObj) {
    rtldInitDfs(gMainObj);
  }
}

static void selfRelocate(uint64_t base, const Elf64_Dyn *dyn) {
  const Elf64_Rela *rela = NULL;
  size_t relasz = 0;

  for (; dyn->d_tag != DT_NULL; dyn++) {
    if (dyn->d_tag == DT_RELA) {
      rela = (const Elf64_Rela *)(base + dyn->d_un.d_ptr);
    } else if (dyn->d_tag == DT_RELASZ) {
      relasz = dyn->d_un.d_val;
    }
  }

  if (rela && relasz > 0) {
    size_t count = relasz / sizeof(Elf64_Rela);
    for (size_t i = 0; i < count; i++) {
      if (ELF64_R_TYPE(rela[i].r_info) == R_X86_64_RELATIVE) {
        uint64_t *target = (uint64_t *)(base + rela[i].r_offset);
        *target = base + (uint64_t)rela[i].r_addend;
      }
    }
  }
}

uint64_t ldMain(uint64_t *sp, const Elf64_Dyn *dynamic) {
  int argc = (int)*sp++;
  char **argv = (char **)sp;
  char **envp = argv + argc + 1;

  char **p = envp;
  while (*p) p++;
  Elf64_Auxv *auxv = (Elf64_Auxv *)(p + 1);

  const Elf64_Phdr *appPhdr = NULL;
  size_t appPhnum = 0;
  uint64_t appEntry = 0;
  uint64_t ldBase = 0;

  for (; auxv->a_type != AT_NULL; auxv++) {
    switch (auxv->a_type) {
      case AT_PHDR:
        appPhdr = (const Elf64_Phdr *)auxv->a_un.a_val;
        break;
      case AT_PHNUM:
        appPhnum = (size_t)auxv->a_un.a_val;
        break;
      case AT_ENTRY:
        appEntry = (uint64_t)auxv->a_un.a_val;
        break;
      case AT_BASE:
        ldBase = (uint64_t)auxv->a_un.a_val;
        break;
      case AT_PAGESZ:
        gPageSize = (size_t)auxv->a_un.a_val;
        break;
    }
  }

  selfRelocate(ldBase, dynamic);

  rtldMemset(&gLdsoObj, 0, sizeof(SharedObject));
  gLdsoObj.base = ldBase;
  gLdsoObj.dynamic = dynamic;
  gLdsoObj.isPinned = 1;
  rtldMemcpy(gLdsoObj.name, "ld-crow.so", 11);
  rtldParseDynamic(&gLdsoObj);
  gLdsoObj.relocated = 1;
  gLdsoObj.next = gObjects;
  gObjects = &gLdsoObj;

  if (appPhdr && appPhnum > 0) {
    uint64_t mainBase = 0;
    const Elf64_Dyn *mainDyn = NULL;

    for (size_t i = 0; i < appPhnum; i++) {
      if (appPhdr[i].p_type == PT_PHDR) {
        mainBase = (uint64_t)appPhdr - appPhdr[i].p_vaddr;
        break;
      }
    }

    if (!mainBase) {
      for (size_t i = 0; i < appPhnum; i++) {
        if (appPhdr[i].p_type == PT_LOAD && appPhdr[i].p_offset == 0) {
          mainBase = (uint64_t)appPhdr - (appPhdr[i].p_vaddr + sizeof(Elf64_Ehdr));
          break;
        }
      }
    }

    for (size_t i = 0; i < appPhnum; i++) {
      if (appPhdr[i].p_type == PT_DYNAMIC) {
        mainDyn = (const Elf64_Dyn *)(mainBase + appPhdr[i].p_vaddr);
        break;
      }
    }

    if (mainDyn) {
      gMainObj = (SharedObject *)rtldAlloc(sizeof(SharedObject));
      rtldMemset(gMainObj, 0, sizeof(SharedObject));
      rtldMemcpy(gMainObj->name, "<main>", 7);
      gMainObj->base = mainBase;
      gMainObj->isPinned = 1;
      gMainObj->phdrs = appPhdr;
      gMainObj->phnum = appPhnum;
      gMainObj->dynamic = mainDyn;

      rtldParseDynamic(gMainObj);

      if (gMainObj->strtab) {
        for (const Elf64_Dyn *dyn = mainDyn; dyn->d_tag != DT_NULL; dyn++) {
          if (dyn->d_tag == DT_NEEDED) {
            const char *depName = gMainObj->strtab + dyn->d_un.d_val;
            SharedObject *depObj = rtldLoadObject(depName);
            if (depObj) {
              rtldAddDependency(gMainObj, depObj);
            }
          }
        }
      }
    }
  }

  rtldRelocateAll();
  rtldCallInit();

  gIsStartingUp = 0;

  return appEntry;
}
