#pragma once
#include <stdint.h>
#include <stddef.h>
#include <idt.h>

#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_MMAP 9
#define SYS_MUNMAP 11
#define SYS_YIELD 24
#define SYS_EXIT 60
#define SYSCALL_HELLO 100

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
#define EBADF 9
#define ENOMEM 12
#define EFAULT 14
#define EINVAL 22
#define ENOSYS 38

#define USER_MMAP_BASE 0x0000600000000000ULL

int64_t sysRead(int Fd, void *Buf, size_t Count);
int64_t sysWrite(int Fd, const void *Buf, size_t Count);
void *sysMmap(void *Addr, size_t Length, int Prot, int Flags, int Fd, int64_t Offset);
int64_t sysMunmap(void *Addr, size_t Length);
int64_t sysYield(void);
void sysExit(int Code) __attribute__((noreturn));

void syscallInit(void);
int64_t syscallDispatch(Registers *Regs);
void syscallEntry(void);
