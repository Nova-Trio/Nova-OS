#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vmm.h>
#include <idt.h>
#include <spinlock.h>
#include <gdt.h>
#include <driver.h>


#define SCHED_DEFAULT_QUANTUM 10
#define SCHED_KSTACK_SIZE (4 * PAGE_SIZE)

#define PERCPU_OFFSET_SELF 0
#define PERCPU_OFFSET_KSTACK_TOP 8
#define PERCPU_OFFSET_SCRATCH_RSP 16
#define PERCPU_OFFSET_CURRENT_THREAD 24
#define PERCPU_OFFSET_CPU_ID 32

#define USER_STACK_TOP_DEFAULT 0x00007FFFFFFFE000ULL
#define USER_STACK_INITIAL_SIZE (8ULL * 1024 * 1024)

typedef struct {
  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
  uint64_t rsp;
  uint64_t ss;
} __attribute__((packed)) IretFrame;

typedef enum {
  THREAD_STATE_UNUSED = 0,
  THREAD_STATE_READY,
  THREAD_STATE_RUNNING,
  THREAD_STATE_BLOCKED,
  THREAD_STATE_SLEEPING,
  THREAD_STATE_ZOMBIE
} ThreadState;

typedef struct {
  uint64_t r15;
  uint64_t r14;
  uint64_t r13;
  uint64_t r12;
  uint64_t rbp;
  uint64_t rbx;
  uint64_t rip;
} __attribute__((packed)) Context;

struct Process;

typedef struct Thread {
  uint64_t rsp;
  uint64_t kstackBase;
  uint64_t kstackTop;
  uint64_t userRsp;

  uint32_t tid;
  ThreadState state;
  uint32_t timeSlice;
  uint32_t defaultSlice;

  volatile uint32_t needResched;
  volatile uint32_t preemptCount;

  uint64_t sleepTargetTicks;

  struct Process *process;
  struct Thread *next;
  struct Thread *prev;

  uint8_t fpuState[512] __attribute__((aligned(16)));
} Thread;

typedef struct Process {
  uint32_t pid;
  PageDirectory pml4;
  union {
    uint64_t pml4Phys;
    uint64_t pml4_phys;
  };
  char name[32];

  union {
    Vma *vmaHead;
    Vma *vma_head;
  };
  union {
    Spinlock vmaLock;
    Spinlock vma_lock;
  };

  DriverHandleTable handleTable;

  Thread *threads;
  struct Process *next;
  struct Process *prev;
} Process;

typedef struct PerCpu {
  struct PerCpu *self;
  union {
    uint64_t kstackTop;
    uint64_t kstack_top;
  };
  union {
    uint64_t userScratchRsp;
    uint64_t user_scratch_rsp;
  };
  union {
    struct Thread *currentThread;
    struct Thread *current_thread;
  };
  union {
    uint32_t cpuId;
    uint32_t cpu_id;
  };
} __attribute__((aligned(16))) PerCpu;

void perCpuInit(void);
static inline PerCpu* perCpuGet(void) {
  PerCpu *cpu;
  __asm__ volatile("mov %%gs:0, %0" : "=r"(cpu));
  return cpu;
}

void schedInit(void);
Process *schedGetKernelProcess(void);
Process *schedCreateProcess(const char *name);
Thread *schedCreateThread(Process *proc, void (*entry)(void *), void *arg, int isUser);

Thread *schedCreateUserThread(Process *proc, uint64_t entry, uint64_t userRsp, uint64_t arg);
void userThreadTrampoline(void);

Process *schedSpawn(const char *path, const char *name, const char **argv, const char **envp);

void schedule(void);
void schedYield(void);
void schedTick(void);

Thread *schedCurrent(void);
void schedPreemptDisable(void);
void schedPreemptEnable(void);

void cpuSwitchTo(Thread *prev, Thread *next);
void threadEntryTrampoline(void);
void schedUnlock(void);
void schedThreadExit(void) __attribute__((noreturn));
void schedSleep(uint64_t ticks);
void schedPreemptFromInterrupt(const Registers *regs);
