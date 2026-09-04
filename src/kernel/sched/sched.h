#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vmm.h>
#include <idt.h>
#include <spinlock.h>

#define SCHED_DEFAULT_QUANTUM 10
#define SCHED_KSTACK_SIZE (4 * PAGE_SIZE)

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
  uint64_t pml4_phys;
  char name[32];

  Thread *threads;
  struct Process *next;
  struct Process *prev;
} Process;

void schedInit(void);
Process* schedCreateProcess(const char* name);
Thread* schedCreateThread(Process* proc, void (*entry)(void*), void* arg, int isUser);

void schedule(void);
void schedYield();
void schedTick();

Thread* schedCurrent(void);
void schedPreemptDisable(void);
void schedPreemptEnable(void);

void cpuSwitchTo(Thread *prev, Thread *next);
void threadEntryTrampoline(void);
void schedUnlock(void);
void schedThreadExit(void);
void schedSleep(uint64_t ticks);
void schedPreemptFromInterrupt(const Registers *Regs);
