#pragma once
#include <stdint.h>
#include <stddef.h>

#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_OPEN 2
#define SYS_CLOSE 3
#define SYS_STAT 4
#define SYS_FSTAT 5
#define SYS_LSEEK 8
#define SYS_MMAP 9
#define SYS_MUNMAP 11
#define SYS_YIELD 24
#define SYS_EXIT 60
#define SYSCALL_HELLO 100
#define SYS_NAG_DISPATCH 110
#define SYS_DRIVER_OPEN 120
#define SYS_DRIVER_IOCTL 121
#define SYS_DRIVER_CLOSE 122

#define O_RDONLY 00000000
#define O_WRONLY 00000001
#define O_RDWR   00000002

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20

typedef struct Stat {
  uint64_t st_dev;
  uint64_t st_ino;
  uint32_t st_mode;
  uint32_t st_nlink;
  uint32_t st_uid;
  uint32_t st_gid;
  uint64_t st_rdev;
  uint64_t st_size;
  uint64_t st_blksize;
  uint64_t st_blocks;
  uint64_t st_atime;
  uint64_t st_mtime;
  uint64_t st_ctime;
} Stat;

static inline int64_t syscall0(int64_t num) {
  int64_t ret;
  __asm__ volatile(
    "syscall"
    : "=a"(ret)
    : "a"(num)
    : "rcx", "r11", "memory"
  );
  return ret;
}

static inline int64_t syscall1(int64_t num, int64_t a1) {
  int64_t ret;
  __asm__ volatile(
    "syscall"
    : "=a"(ret)
    : "a"(num), "D"(a1)
    : "rcx", "r11", "memory"
  );
  return ret;
}

static inline int64_t syscall2(int64_t num, int64_t a1, int64_t a2) {
  int64_t ret;
  __asm__ volatile(
    "syscall"
    : "=a"(ret)
    : "a"(num), "D"(a1), "S"(a2)
    : "rcx", "r11", "memory"
  );
  return ret;
}

static inline int64_t syscall3(int64_t num, int64_t a1, int64_t a2, int64_t a3) {
  int64_t ret;
  __asm__ volatile(
    "syscall"
    : "=a"(ret)
    : "a"(num), "D"(a1), "S"(a2), "d"(a3)
    : "rcx", "r11", "memory"
  );
  return ret;
}

static inline int64_t syscall6(int64_t num, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
  int64_t ret;
  register int64_t r10 __asm__("r10") = a4;
  register int64_t r8  __asm__("r8")  = a5;
  register int64_t r9  __asm__("r9")  = a6;
  __asm__ volatile(
    "syscall"
    : "=a"(ret)
    : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
    : "rcx", "r11", "memory"
  );
  return ret;
}

#ifndef _CROWOS_NO_POSIX_WRAPPERS
static inline int64_t write(int fd, const void *buf, size_t count) {
  return syscall3(SYS_WRITE, fd, (int64_t)buf, (int64_t)count);
}

static inline int64_t read(int fd, void *buf, size_t count) {
  return syscall3(SYS_READ, fd, (int64_t)buf, (int64_t)count);
}

static inline int open(const char *path, int flags) {
  return (int)syscall3(SYS_OPEN, (int64_t)path, (int64_t)flags, 0);
}

static inline int close(int fd) {
  return (int)syscall1(SYS_CLOSE, (int64_t)fd);
}

static inline int64_t lseek(int fd, int64_t offset, int whence) {
  return syscall3(SYS_LSEEK, (int64_t)fd, offset, (int64_t)whence);
}

static inline int stat(const char *path, Stat *buf) {
  return (int)syscall2(SYS_STAT, (int64_t)path, (int64_t)buf);
}

static inline int fstat(int fd, Stat *buf) {
  return (int)syscall2(SYS_FSTAT, (int64_t)fd, (int64_t)buf);
}

static inline void *mmap(void *addr, size_t length, int prot, int flags, int fd, int64_t offset) {
  return (void *)syscall6(SYS_MMAP, (int64_t)addr, (int64_t)length, prot, flags, fd, offset);
}

static inline int munmap(void *addr, size_t length) {
  return (int)syscall2(SYS_MUNMAP, (int64_t)addr, (int64_t)length);
}
#endif

static inline void yield(void) {
  syscall0(SYS_YIELD);
}

static inline int64_t nagDispatch(uint32_t adapterId, uint32_t op, void* arg, size_t argSize){
  return syscall6(SYS_NAG_DISPATCH, (int64_t)adapterId, (int64_t)op, (int64_t)arg, (int64_t)argSize, 0, 0);
}

static inline int driverOpen(const char *name) {
  return (int)syscall1(SYS_DRIVER_OPEN, (int64_t)name);
}

static inline int64_t driverIoctl(int handle, uint32_t cmd, void *arg, size_t arg_size) {
  return syscall6(SYS_DRIVER_IOCTL, (int64_t)handle, (int64_t)cmd, (int64_t)arg, (int64_t)arg_size, 0, 0);
}

static inline int driverClose(int handle) {
  return (int)syscall1(SYS_DRIVER_CLOSE, (int64_t)handle);
}

__attribute__((noreturn)) void exit(int status);

static inline size_t user_strlen(const char *s) {
  size_t len = 0;
  while (s[len]) len++;
  return len;
}

static inline void print(const char *s) {
  syscall3(SYS_WRITE, 1, (int64_t)s, user_strlen(s));
}
