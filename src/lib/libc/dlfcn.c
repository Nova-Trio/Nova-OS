#include "dlfcn.h"
#include <stddef.h>

__attribute__((weak)) void *dlopen(const char *filename, int flags) {
  (void)filename;
  (void)flags;
  return NULL;
}

__attribute__((weak)) void *dlsym(void *handle, const char *symbol) {
  (void)handle;
  (void)symbol;
  return NULL;
}

__attribute__((weak)) int dlclose(void *handle) {
  (void)handle;
  return -1;
}

__attribute__((weak)) char *dlerror(void) {
  return "Dynamic linker not loaded";
}
