#include "sched.h"
#include <vmm.h>
#include <heap.h>
#include <string.h>
#include <gdt.h>
#include <console.h>

static Spinlock g_schedLock = SPINLOCK_INIT;

static Process *g_kernelProcess = NULL;
static Thread *g_kernelThread = NULL;
static Thread *g_currentThread = NULL;
static Thread *g_idleThread = NULL;

static Thread *g_readyQueueHead = NULL;
static Thread *g_readyQueueTail = NULL;
static Thread *g_sleepQueueHead = NULL;

static volatile uint64_t g_schedTicks = 0;
static volatile uint32_t g_nextPid = 0;
static volatile uint32_t g_nextTid = 0;

static void enqueueReady(Thread *thread) {
  thread->state = THREAD_STATE_READY;
  thread->next = NULL;
  thread->prev = g_readyQueueTail;

  if (g_readyQueueTail) {
    g_readyQueueTail->next = thread;
  } else {
    g_readyQueueHead = thread;
  }
  g_readyQueueTail = thread;
}

static Thread *dequeueReady(void) {
  Thread *thread = g_readyQueueHead;
  if (!thread) {
    return NULL;
  }

  g_readyQueueHead = thread->next;
  if (g_readyQueueHead) {
    g_readyQueueHead->prev = NULL;
  } else {
    g_readyQueueTail = NULL;
  }

  thread->next = NULL;
  thread->prev = NULL;
  return thread;
}

static void idleTask(void *arg) {
  (void)arg;
  while (1) {
    __asm__ volatile("pause; hlt");
  }
}

void schedUnlock(void) {
  spin_unlock(&g_schedLock);
}

Thread *schedCurrent(void) {
  return g_currentThread;
}

void schedPreemptDisable(void) {
  if (g_currentThread) {
    g_currentThread->preemptCount++;
  }
}

void schedPreemptEnable(void) {
  if (g_currentThread) {
    g_currentThread->preemptCount--;
    if (g_currentThread->preemptCount == 0 && g_currentThread->needResched) {
      schedule();
    }
  }
}

Process *schedCreateProcess(const char *name) {
  Process *proc = (Process *)kzalloc(sizeof(Process));
  if (!proc) {
    return NULL;
  }

  proc->pid = __atomic_fetch_add(&g_nextPid, 1, __ATOMIC_RELAXED);
  if (proc->pid == 0) {
    proc->pml4 = vmm_get_kernel_pml4();
  } else {
    proc->pml4 = vmmCreateAddressSpace();
    if (!proc->pml4) {
      kfree(proc);
      return NULL;
    }
  }

  proc->pml4_phys = vmm_virt_to_phys(vmm_get_kernel_pml4(), (uint64_t)proc->pml4);
  if (name) {
    size_t len = strlen(name);
    if (len >= sizeof(proc->name)) {
      len = sizeof(proc->name) - 1;
    }
    memcpy(proc->name, name, len);
    proc->name[len] = '\0';
  }

  return proc;
}

Thread *schedCreateThread(Process *proc, void (*entry)(void *), void *arg, int isUser) {
  (void)isUser;
  if (!proc || !entry) {
    return NULL;
  }

  Thread *thread = (Thread *)kzalloc(sizeof(Thread));
  if (!thread) {
    return NULL;
  }

  void *stackMem = kmalloc(SCHED_KSTACK_SIZE);
  if (!stackMem) {
    kfree(thread);
    return NULL;
  }

  thread->kstackBase = (uint64_t)stackMem;
  thread->kstackTop = (thread->kstackBase + SCHED_KSTACK_SIZE) & ~0xFULL;
  thread->tid = __atomic_fetch_add(&g_nextTid, 1, __ATOMIC_RELAXED);
  thread->process = proc;
  thread->timeSlice = SCHED_DEFAULT_QUANTUM;
  thread->defaultSlice = SCHED_DEFAULT_QUANTUM;
  thread->state = THREAD_STATE_READY;
  thread->needResched = 0;
  thread->preemptCount = 0;

  *(uint16_t *)&thread->fpuState[0] = 0x037F;
  *(uint32_t *)&thread->fpuState[24] = 0x1F80;

  Context *ctx = (Context *)(thread->kstackTop - sizeof(Context));
  memset(ctx, 0, sizeof(Context));
  ctx->r12 = (uint64_t)entry;
  ctx->r13 = (uint64_t)arg;
  ctx->rip = (uint64_t)threadEntryTrampoline;
  thread->rsp = (uint64_t)ctx;

  uint64_t rflags = spin_lock_irqsave(&g_schedLock);
  thread->next = proc->threads;
  proc->threads = thread;

  enqueueReady(thread);
  spin_unlock_irqrestore(&g_schedLock, rflags);

  return thread;
}

void schedule(void) {
  uint64_t rflags = spin_lock_irqsave(&g_schedLock);

  Thread *prev = g_currentThread;
  if (prev && prev->preemptCount > 0) {
    spin_unlock_irqrestore(&g_schedLock, rflags);
    return;
  }

  Thread *next = dequeueReady();
  if (!next) {
    if (prev && prev->state == THREAD_STATE_RUNNING) {
      next = prev;
    } else {
      next = g_idleThread;
    }
  }

  if (next == prev) {
    if (prev) {
      prev->needResched = 0;
    }
    spin_unlock_irqrestore(&g_schedLock, rflags);
    return;
  }

  if (prev) {
    if (prev->state == THREAD_STATE_RUNNING) {
      prev->state = THREAD_STATE_READY;
      if (prev != g_idleThread) {
        enqueueReady(prev);
      }
    }
    __asm__ volatile("fxsave64 %0" : "=m"(prev->fpuState));
  }

  next->state = THREAD_STATE_RUNNING;
  next->needResched = 0;
  next->timeSlice = next->defaultSlice;
  g_currentThread = next;

  if (next->process && next->process->pml4) {
    if (!prev || !prev->process || prev->process != next->process) {
      vmm_switch_pml4(next->process->pml4);
    }
  }

  tss_set_rsp0(next->kstackTop);
  __asm__ volatile("fxrstor64 %0" : : "m"(next->fpuState));

  cpuSwitchTo(prev, next);

  spin_unlock_irqrestore(&g_schedLock, rflags);
}

void schedYield(void) {
  if (g_currentThread) {
    g_currentThread->needResched = 1;
  }
  schedule();
}

void schedSleep(uint64_t ticks) {
  if (!g_currentThread || ticks == 0) {
    return;
  }

  uint64_t rflags = spin_lock_irqsave(&g_schedLock);
  g_currentThread->sleepTargetTicks = g_schedTicks + ticks;
  g_currentThread->state = THREAD_STATE_SLEEPING;

  g_currentThread->next = g_sleepQueueHead;
  g_currentThread->prev = NULL;
  if (g_sleepQueueHead) {
    g_sleepQueueHead->prev = g_currentThread;
  }
  g_sleepQueueHead = g_currentThread;

  spin_unlock_irqrestore(&g_schedLock, rflags);
  schedule();
}

void schedTick(void) {
  uint64_t rflags = spin_lock_irqsave(&g_schedLock);
  g_schedTicks++;

  Thread *currSleep = g_sleepQueueHead;
  while (currSleep) {
    Thread *nextSleep = currSleep->next;
    if (g_schedTicks >= currSleep->sleepTargetTicks) {
      if (currSleep->prev) {
        currSleep->prev->next = currSleep->next;
      } else {
        g_sleepQueueHead = currSleep->next;
      }
      if (currSleep->next) {
        currSleep->next->prev = currSleep->prev;
      }

      currSleep->next = NULL;
      currSleep->prev = NULL;
      enqueueReady(currSleep);

      if (g_currentThread) {
        g_currentThread->needResched = 1;
      }
    }
    currSleep = nextSleep;
  }

  if (g_currentThread) {
    if (g_currentThread == g_idleThread) {
      if (g_readyQueueHead) {
        g_currentThread->needResched = 1;
      }
    } else if (g_currentThread->timeSlice > 0) {
      g_currentThread->timeSlice--;
      if (g_currentThread->timeSlice == 0) {
        g_currentThread->needResched = 1;
      }
    }
  }

  spin_unlock_irqrestore(&g_schedLock, rflags);
}

void schedPreemptFromInterrupt(const Registers *Regs) {
  Thread *CurrentThread = schedCurrent();
  if (!CurrentThread || CurrentThread->preemptCount > 0 || !CurrentThread->needResched) {
    return;
  }

  if (Regs && !(Regs->rflags & 0x200)) {
    return;
  }

  schedule();
}

void schedThreadExit(void) {
  uint64_t rflags = spin_lock_irqsave(&g_schedLock);
  if (g_currentThread) {
    g_currentThread->state = THREAD_STATE_ZOMBIE;
  }
  spin_unlock_irqrestore(&g_schedLock, rflags);

  schedule();

  while (1) {
    __asm__ volatile("pause; hlt");
  }
}

void schedInit(void) {
  g_kernelProcess = schedCreateProcess("kernel");

  g_kernelThread = (Thread *)kzalloc(sizeof(Thread));
  g_kernelThread->tid = __atomic_fetch_add(&g_nextTid, 1, __ATOMIC_RELAXED);
  g_kernelThread->state = THREAD_STATE_RUNNING;
  g_kernelThread->process = g_kernelProcess;
  g_kernelThread->timeSlice = SCHED_DEFAULT_QUANTUM;
  g_kernelThread->defaultSlice = SCHED_DEFAULT_QUANTUM;
  g_kernelThread->preemptCount = 0;
  g_kernelThread->needResched = 0;

  g_kernelProcess->threads = g_kernelThread;
  g_currentThread = g_kernelThread;

  g_idleThread = schedCreateThread(g_kernelProcess, idleTask, NULL, 0);

  uint64_t rflags = spin_lock_irqsave(&g_schedLock);
  if (g_readyQueueHead == g_idleThread) {
    g_readyQueueHead = g_idleThread->next;
    if (g_readyQueueHead) {
      g_readyQueueHead->prev = NULL;
    } else {
      g_readyQueueTail = NULL;
    }
  } else if (g_idleThread->prev) {
    g_idleThread->prev->next = g_idleThread->next;
    if (g_idleThread->next) {
      g_idleThread->next->prev = g_idleThread->prev;
    } else {
      g_readyQueueTail = g_idleThread->prev;
    }
  }
  g_idleThread->next = NULL;
  g_idleThread->prev = NULL;
  g_idleThread->state = THREAD_STATE_READY;
  spin_unlock_irqrestore(&g_schedLock, rflags);
}
