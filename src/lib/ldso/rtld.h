#pragma once
#include <stdint.h>
#include <stddef.h>
#include "elf.h"
#include <novaos.h>

typedef struct SharedObject {
  char name[128];
  uint64_t base;
  uint64_t mapBase;
  size_t totalSpan;

  const Elf64_Phdr *phdrs;
  size_t phnum;
  const Elf64_Dyn *dynamic;

  const char *strtab;
  const Elf64_Sym *symtab;
  const Elf64_Rela *rela;
  size_t relasz;
  const Elf64_Rela *jmprel;
  size_t pltrelsz;

  const uint32_t *hash;
  const uint32_t *gnuHash;

  void (*init)(void);
  void (**initArray)(void);
  size_t initArraySz;

  void (*fini)(void);
  void (**finiArray)(void);
  size_t finiArraySz;

  struct SharedObject **deps;
  size_t depCount;
  size_t depCapacity;

  int relocated;
  int initCalled;
  int finiCalled;
  int visiting;
  int isPinned;
  int refCount;

  struct SharedObject *next;
} SharedObject;

void *rtldDlOpen(const char *filename, int flags);
void *rtldDlSym(void *handle, const char *symbol);
int rtldDlClose(void *handle);
char *rtldDlError(void);
void rtldCallFini(void);

void *rtldAlloc(size_t size);
size_t rtldStrlen(const char *s);
int rtldStrcmp(const char *s1, const char *s2);
int rtldStrncmp(const char *s1, const char *s2, size_t n);
void *rtldMemcpy(void *dest, const void *src, size_t n);
void *rtldMemset(void *s, int c, size_t n);
char *rtldStrdup(const char *s);

uint32_t rtldElfHash(const char *name);
uint32_t rtldGnuHash(const char *name);

SharedObject *rtldLoadObject(const char *name);
void rtldRelocateAll(void);
void rtldCallInit(void);

uint64_t ldMain(uint64_t *sp, const Elf64_Dyn *dynamic);
