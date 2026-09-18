#define _CROWOS_NO_POSIX_WRAPPERS 1
#include "unistd.h"
#include "fcntl.h"
#include "sys/mman.h"
#include <novaos.h>

int64_t read(int fd, void *buf, size_t count) {
  return syscall3(SYS_READ, fd, (int64_t)buf, (int64_t)count);
}

int64_t write(int fd, const void *buf, size_t count) {
  return syscall3(SYS_WRITE, fd, (int64_t)buf, (int64_t)count);
}

int open(const char *pathname, int flags, ...) {
  return (int)syscall3(SYS_OPEN, (int64_t)pathname, (int64_t)flags, 0);
}

int close(int fd) {
  return (int)syscall1(SYS_CLOSE, (int64_t)fd);
}

int64_t lseek(int fd, int64_t offset, int whence) {
  return syscall3(SYS_LSEEK, (int64_t)fd, offset, (int64_t)whence);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, int64_t offset) {
  return (void *)syscall6(SYS_MMAP, (int64_t)addr, (int64_t)length, prot, flags, fd, offset);
}

int munmap(void *addr, size_t length) {
  return (int)syscall2(SYS_MUNMAP, (int64_t)addr, (int64_t)length);
}
