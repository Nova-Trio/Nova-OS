#define _CROWOS_NO_POSIX_WRAPPERS 1
#include <stdlib.h>
#include "string.h"
#include "sys/mman.h"
#include <novaos.h>

#define CHUNK_MAGIC_ALLOC 0x414C4F43U // 'ALOC'
#define CHUNK_MAGIC_FREE  0x46524545U // 'FREE'
#define LARGE_ALLOC_THRESHOLD (128 * 1024)
#define ARENA_DEFAULT_SIZE (64 * 1024)

struct Arena;

typedef struct Chunk {
  uint32_t magic;
  uint32_t isMmap;
  size_t size;
  struct Chunk *next;
  struct Chunk *prev;
  struct Chunk *prevPhys;
  struct Arena *arena;
} __attribute__((aligned(16))) Chunk;

typedef struct Arena {
  size_t totalSize;
  size_t activeChunks;
  struct Arena *next;
  struct Arena *prev;
} __attribute__((aligned(16))) Arena;

typedef struct AtExitNode {
  void (*fn)(void *);
  void *arg;
  void *dso;
  struct AtExitNode *next;
} AtExitNode;

static Chunk *gFreeList = NULL;
static Arena *gArenaList = NULL;
static size_t gArenaCount = 0;
static AtExitNode *gAtExitList = NULL;
static volatile int gMallocLock = 0;

static inline void mallocLock(void) {
  while (__atomic_test_and_set(&gMallocLock, __ATOMIC_ACQUIRE)) {
    __asm__ volatile("pause");
  }
}

static inline void mallocUnlock(void) {
  __atomic_clear(&gMallocLock, __ATOMIC_RELEASE);
}

static void addToFreeList(Chunk *c) {
  c->next = gFreeList;
  c->prev = NULL;
  if (gFreeList) {
    gFreeList->prev = c;
  }
  gFreeList = c;
}

static void removeFromFreeList(Chunk *c) {
  if (c->prev) {
    c->prev->next = c->next;
  } else {
    gFreeList = c->next;
  }
  if (c->next) {
    c->next->prev = c->prev;
  }
  c->next = NULL;
  c->prev = NULL;
}

static void removeFromArenaList(Arena *a) {
  if (a->prev) {
    a->prev->next = a->next;
  } else {
    gArenaList = a->next;
  }
  if (a->next) {
    a->next->prev = a->prev;
  }
  gArenaCount--;
}

void *malloc(size_t size) {
  if (size == 0) {
    return NULL;
  }
  size = (size + 15) & ~15ULL;

  if (size >= LARGE_ALLOC_THRESHOLD) {
    size_t total = size + sizeof(Chunk);
    size_t pages = (total + 4095) & ~4095ULL;
    void *mem = mmap(NULL, pages, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((int64_t)mem < 0 || !mem) {
      return NULL;
    }

    Chunk *c = (Chunk *)mem;
    c->magic = CHUNK_MAGIC_ALLOC;
    c->isMmap = 1;
    c->size = pages - sizeof(Chunk);
    c->next = NULL;
    c->prev = NULL;
    c->prevPhys = NULL;
    c->arena = NULL;
    return (void *)(c + 1);
  }

  mallocLock();

  Chunk *curr = gFreeList;
  while (curr) {
    if (curr->size >= size) {
      removeFromFreeList(curr);

      if (curr->size >= size + sizeof(Chunk) + 16) {
        Chunk *split = (Chunk *)((uint8_t *)(curr + 1) + size);
        split->magic = CHUNK_MAGIC_FREE;
        split->isMmap = 0;
        split->size = curr->size - size - sizeof(Chunk);
        split->prevPhys = curr;
        split->arena = curr->arena;

        Chunk *nextPhys = (Chunk *)((uint8_t *)(split + 1) + split->size);
        if ((uint8_t *)nextPhys < (uint8_t *)curr->arena + curr->arena->totalSize) {
          nextPhys->prevPhys = split;
        }

        addToFreeList(split);
        curr->size = size;
      }

      curr->magic = CHUNK_MAGIC_ALLOC;
      if (curr->arena) {
        curr->arena->activeChunks++;
      }

      mallocUnlock();
      return (void *)(curr + 1);
    }
    curr = curr->next;
  }

  size_t needed = sizeof(Arena) + sizeof(Chunk) + size;
  size_t arenaSz = ARENA_DEFAULT_SIZE;
  if (needed > arenaSz) {
    arenaSz = (needed + 4095) & ~4095ULL;
  }

  void *mem = mmap(NULL, arenaSz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if ((int64_t)mem < 0 || !mem) {
    mallocUnlock();
    return NULL;
  }

  Arena *arena = (Arena *)mem;
  arena->totalSize = arenaSz;
  arena->activeChunks = 1;
  arena->next = gArenaList;
  arena->prev = NULL;
  if (gArenaList) {
    gArenaList->prev = arena;
  }
  gArenaList = arena;
  gArenaCount++;

  Chunk *allocatedChunk = (Chunk *)((uint8_t *)arena + sizeof(Arena));
  allocatedChunk->magic = CHUNK_MAGIC_ALLOC;
  allocatedChunk->isMmap = 0;
  allocatedChunk->size = size;
  allocatedChunk->prevPhys = NULL;
  allocatedChunk->arena = arena;
  allocatedChunk->next = NULL;
  allocatedChunk->prev = NULL;

  size_t remainder = arenaSz - sizeof(Arena) - sizeof(Chunk) - size;
  if (remainder >= sizeof(Chunk) + 16) {
    Chunk *remChunk = (Chunk *)((uint8_t *)(allocatedChunk + 1) + size);
    remChunk->magic = CHUNK_MAGIC_FREE;
    remChunk->isMmap = 0;
    remChunk->size = remainder - sizeof(Chunk);
    remChunk->prevPhys = allocatedChunk;
    remChunk->arena = arena;
    addToFreeList(remChunk);
  }

  mallocUnlock();
  return (void *)(allocatedChunk + 1);
}

void free(void *ptr) {
  if (!ptr) {
    return;
  }

  Chunk *c = ((Chunk *)ptr) - 1;
  if (c->magic != CHUNK_MAGIC_ALLOC) {
    return;
  }
  c->magic = CHUNK_MAGIC_FREE;

  if (c->isMmap) {
    size_t total = c->size + sizeof(Chunk);
    munmap((void *)c, total);
    return;
  }

  mallocLock();

  if (c->arena) {
    if (c->arena->activeChunks > 0) {
      c->arena->activeChunks--;
    }
  }

  Chunk *nextPhys = (Chunk *)((uint8_t *)(c + 1) + c->size);
  if ((uint8_t *)nextPhys < (uint8_t *)c->arena + c->arena->totalSize) {
    if (nextPhys->magic == CHUNK_MAGIC_FREE) {
      removeFromFreeList(nextPhys);
      c->size += sizeof(Chunk) + nextPhys->size;

      Chunk *afterNext = (Chunk *)((uint8_t *)(c + 1) + c->size);
      if ((uint8_t *)afterNext < (uint8_t *)c->arena + c->arena->totalSize) {
        afterNext->prevPhys = c;
      }
    }
  }

  Chunk *prevPhys = c->prevPhys;
  if (prevPhys && prevPhys->magic == CHUNK_MAGIC_FREE) {
    removeFromFreeList(prevPhys);
    prevPhys->size += sizeof(Chunk) + c->size;

    Chunk *afterC = (Chunk *)((uint8_t *)(prevPhys + 1) + prevPhys->size);
    if ((uint8_t *)afterC < (uint8_t *)c->arena + c->arena->totalSize) {
      afterC->prevPhys = prevPhys;
    }
    c = prevPhys;
  }

  if (c->arena && c->arena->activeChunks == 0 && gArenaCount > 1) {
    if (c->prevPhys == NULL && ((uint8_t *)(c + 1) + c->size == (uint8_t *)c->arena + c->arena->totalSize)) {
      removeFromArenaList(c->arena);
      munmap((void *)c->arena, c->arena->totalSize);
      mallocUnlock();
      return;
    }
  }

  addToFreeList(c);
  mallocUnlock();
}

void *calloc(size_t nmemb, size_t size) {
  if (size != 0 && nmemb > ((size_t)-1) / size) {
    return NULL;
  }
  size_t total = nmemb * size;
  void *p = malloc(total);
  if (p) {
    memset(p, 0, total);
  }
  return p;
}

void *realloc(void *ptr, size_t size) {
  if (!ptr) {
    return malloc(size);
  }
  if (size == 0) {
    free(ptr);
    return NULL;
  }

  Chunk *c = ((Chunk *)ptr) - 1;
  if (c->magic != CHUNK_MAGIC_ALLOC) {
    return NULL;
  }

  size = (size + 15) & ~15ULL;

  if (c->isMmap) {
    if (c->size >= size && (c->size - size) < 4096) {
      return ptr;
    }
    void *newMmapPtr = malloc(size);
    if (!newMmapPtr) return NULL;
    size_t copyLen = (c->size < size) ? c->size : size;
    memcpy(newMmapPtr, ptr, copyLen);
    free(ptr);
    return newMmapPtr;
  }

  mallocLock();

  if (c->size >= size) {
    if (c->size >= size + sizeof(Chunk) + 16) {
      Chunk *split = (Chunk *)((uint8_t *)(c + 1) + size);
      split->magic = CHUNK_MAGIC_FREE;
      split->isMmap = 0;
      split->size = c->size - size - sizeof(Chunk);
      split->prevPhys = c;
      split->arena = c->arena;

      Chunk *nextPhys = (Chunk *)((uint8_t *)(split + 1) + split->size);
      if ((uint8_t *)nextPhys < (uint8_t *)c->arena + c->arena->totalSize) {
        nextPhys->prevPhys = split;
      }

      c->size = size;

      if (nextPhys < (Chunk *)((uint8_t *)c->arena + c->arena->totalSize) && nextPhys->magic == CHUNK_MAGIC_FREE) {
        removeFromFreeList(nextPhys);
        split->size += sizeof(Chunk) + nextPhys->size;
        Chunk *afterNext = (Chunk *)((uint8_t *)(split + 1) + split->size);
        if ((uint8_t *)afterNext < (uint8_t *)c->arena + c->arena->totalSize) {
          afterNext->prevPhys = split;
        }
      }

      addToFreeList(split);
    }
    mallocUnlock();
    return ptr;
  }

  Chunk *nextPhys = (Chunk *)((uint8_t *)(c + 1) + c->size);
  if ((uint8_t *)nextPhys < (uint8_t *)c->arena + c->arena->totalSize && nextPhys->magic == CHUNK_MAGIC_FREE) {
    size_t combined = c->size + sizeof(Chunk) + nextPhys->size;
    if (combined >= size) {
      removeFromFreeList(nextPhys);
      c->size = combined;

      Chunk *afterNext = (Chunk *)((uint8_t *)(c + 1) + c->size);
      if ((uint8_t *)afterNext < (uint8_t *)c->arena + c->arena->totalSize) {
        afterNext->prevPhys = c;
      }

      if (c->size >= size + sizeof(Chunk) + 16) {
        Chunk *split = (Chunk *)((uint8_t *)(c + 1) + size);
        split->magic = CHUNK_MAGIC_FREE;
        split->isMmap = 0;
        split->size = c->size - size - sizeof(Chunk);
        split->prevPhys = c;
        split->arena = c->arena;

        Chunk *nextNext = (Chunk *)((uint8_t *)(split + 1) + split->size);
        if ((uint8_t *)nextNext < (uint8_t *)c->arena + c->arena->totalSize) {
          nextNext->prevPhys = split;
        }

        c->size = size;
        addToFreeList(split);
      }

      mallocUnlock();
      return ptr;
    }
  }

  mallocUnlock();

  void *newPtr = malloc(size);
  if (!newPtr) {
    return NULL;
  }

  size_t copyLen = (c->size < size) ? c->size : size;
  memcpy(newPtr, ptr, copyLen);
  free(ptr);
  return newPtr;
}

int __cxa_atexit(void (*fn)(void *), void *arg, void *dso) {
  if (!fn) {
    return 0;
  }

  AtExitNode *node = (AtExitNode *)malloc(sizeof(AtExitNode));
  if (!node) {
    return -1;
  }

  node->fn = fn;
  node->arg = arg;
  node->dso = dso;

  mallocLock();
  node->next = gAtExitList;
  gAtExitList = node;
  mallocUnlock();

  return 0;
}

static void atexitBridge(void *arg) {
  void (*fn)(void) = (void (*)(void))arg;
  if (fn) {
    fn();
  }
}

int atexit(void (*fn)(void)) {
  return __cxa_atexit(atexitBridge, (void *)fn, NULL);
}

void rtldCallFini(void) __attribute__((weak));

void exit(int status) {
  mallocLock();
  AtExitNode *curr = gAtExitList;
  gAtExitList = NULL;
  mallocUnlock();

  while (curr) {
    AtExitNode *next = curr->next;
    if (curr->fn) {
      curr->fn(curr->arg);
    }
    free(curr);
    curr = next;
  }

  if (rtldCallFini) {
    rtldCallFini();
  }

  syscall1(SYS_EXIT, status);
  while (1) {
    __asm__ volatile("hlt");
  }
}

void abort(void) {
  exit(134);
}

int atoi(const char *s) {
  int sign = 1;
  while (*s == ' ' || (*s >= 9 && *s <= 13)) {
    s++;
  }
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+') {
    s++;
  }

  int64_t res = 0;
  while (*s >= '0' && *s <= '9') {
    res = res * 10 + (*s - '0');
    if (sign == 1 && res > 2147483647LL) {
      return 2147483647;
    }
    if (sign == -1 && -res < -2147483648LL) {
      return (int)-2147483648LL;
    }
    s++;
  }

  return (int)(sign * res);
}

int abs(int j) {
  if (j == -2147483648) {
    return 2147483647;
  }
  return j < 0 ? -j : j;
}
