#pragma once
#include <stdint.h>
#include <stddef.h>
#include <idt.h>

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
#define O_RDWR 00000002

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define S_IFMT 0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_IFCHR 0020000

#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20

#define EPERM 1
#define ENOENT 2
#define EIO 5
#define EBADF 9
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EISDIR 21
#define EINVAL 22
#define EMFILE 24
#define ESPIPE 29
#define ENOSYS 38

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

#define USER_MMAP_BASE 0x0000600000000000ULL

struct Process;

int64_t sysOpen(const char *Path, int Flags, uint32_t Mode);
int64_t sysClose(int Fd);
int64_t sysRead(int Fd, void *Buf, size_t Count);
int64_t sysWrite(int Fd, const void *Buf, size_t Count);
int64_t sysLseek(int Fd, int64_t Offset, int Whence);
int64_t sysStat(const char *Path, Stat *StatBuf);
int64_t sysFstat(int Fd, Stat *StatBuf);
void *sysMmap(void *Addr, size_t Length, int Prot, int Flags, int Fd, int64_t Offset);
int64_t sysMunmap(void *Addr, size_t Length);
int64_t sysYield(void);
void sysExit(int Code) __attribute__((noreturn));
void fileTableCleanup(struct Process *proc);

void syscallInit(void);
int64_t syscallDispatch(Registers *Regs);
void syscallEntry(void);

int64_t sysNagDispatch(uint32_t adapterId, uint32_t op, void* arg, size_t argSize);

int64_t sysDriverOpen(const char *name);
int64_t sysDriverIoctl(int handle, uint32_t cmd, void *arg, size_t argSize);
int64_t sysDriverClose(int handle);
