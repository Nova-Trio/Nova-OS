#include "sched.h"
#include <stdint.h>
#include <vmm.h>
#include <pmm.h>
#include <heap.h>
#include <string.h>
#include <gdt.h>
#include <elf.h>
#include <fat32.h>
#include <console.h>

#define MSR_GS_BASE 0xC0000101u
#define MSR_KERNEL_GS_BASE 0xC0000102u

static Spinlock gSchedLock = SPINLOCK_INIT;

static Process *gKernelProcess = NULL;
static Thread *gKernelThread = NULL;
static Thread *gCurrentThread = NULL;
static Thread *gIdleThread = NULL;

static Thread *gReadyQueueHead = NULL;
static Thread *gReadyQueueTail = NULL;
static Thread *gSleepQueueHead = NULL;

static volatile uint64_t gSchedTicks = 0;
static volatile uint32_t gNextPid = 0;
static volatile uint32_t gNextTid = 0;

static PerCpu bspPerCpu;

static inline void wrmsr64(uint32_t msr, uint64_t val) {
  uint32_t low = (uint32_t)val;
  uint32_t high = (uint32_t)(val >> 32);
  __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr) : "memory");
}

void perCpuInit(void) {
  bspPerCpu.self = &bspPerCpu;
  bspPerCpu.kstackTop = 0;
  bspPerCpu.userScratchRsp = 0;
  bspPerCpu.currentThread = gKernelThread;
  bspPerCpu.cpuId = 0;

  wrmsr64(MSR_GS_BASE, (uint64_t)&bspPerCpu);
  wrmsr64(MSR_KERNEL_GS_BASE, 0);
}

static void enqueueReady(Thread *thread) {
  thread->state = THREAD_STATE_READY;
  thread->next = NULL;
  thread->prev = gReadyQueueTail;

  if (gReadyQueueTail) {
    gReadyQueueTail->next = thread;
  } else {
    gReadyQueueHead = thread;
  }
  gReadyQueueTail = thread;
}

static Thread *dequeueReady(void) {
  Thread *thread = gReadyQueueHead;
  if (!thread) {
    return NULL;
  }

  gReadyQueueHead = thread->next;
  if (gReadyQueueHead) {
    gReadyQueueHead->prev = NULL;
  } else {
    gReadyQueueTail = NULL;
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
  spin_unlock(&gSchedLock);
}

Thread *schedCurrent(void) {
  return gCurrentThread;
}

void schedPreemptDisable(void) {
  if (gCurrentThread) {
    gCurrentThread->preemptCount++;
  }
}

void schedPreemptEnable(void) {
  if (gCurrentThread) {
    gCurrentThread->preemptCount--;
    if (gCurrentThread->preemptCount == 0 && gCurrentThread->needResched) {
      schedule();
    }
  }
}

Process *schedCreateProcess(const char *name) {
  Process *proc = (Process *)kzalloc(sizeof(Process));
  if (!proc) {
    return NULL;
  }

  proc->pid = __atomic_fetch_add(&gNextPid, 1, __ATOMIC_RELAXED);
  if (proc->pid == 0) {
    proc->pml4 = vmmGetKernelPml4();
  } else {
    proc->pml4 = vmmCreateAddressSpace();
    if (!proc->pml4) {
      kfree(proc);
      return NULL;
    }
  }

  proc->vmaHead = NULL;
  spinlock_init(&proc->vmaLock);

  proc->pml4Phys = vmmVirtToPhys(vmmGetKernelPml4(), (uint64_t)proc->pml4);
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

Thread *schedCreateUserThread(Process *proc, uint64_t entry, uint64_t userRsp, uint64_t arg) {
  if (!proc || entry == 0) {
    return NULL;
  }

  if (userRsp == 0) {
    uint64_t stackTop = USER_STACK_TOP_DEFAULT;
    uint64_t stackSize = USER_STACK_INITIAL_SIZE;
    uint64_t stackBase = stackTop - stackSize;

    uint64_t rflags = spin_lock_irqsave(&proc->vmaLock);
    while (vmaFind(proc, stackBase) != NULL || vmaFind(proc, stackTop - 8) != NULL) {
      stackTop -= (USER_STACK_MAX_SIZE + PAGE_SIZE);
      stackBase = stackTop - stackSize;
      if (stackBase < 0x0000000001000000ULL) {
        spin_unlock_irqrestore(&proc->vmaLock, rflags);
        return NULL;
      }
    }
    spin_unlock_irqrestore(&proc->vmaLock, rflags);

    if (!vmaCreate(proc, stackBase, stackSize, VMA_READ | VMA_WRITE | VMA_USER | VMA_STACK | VMA_ANON)) {
      return NULL;
    }
    userRsp = stackTop - 8;
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
  thread->tid = __atomic_fetch_add(&gNextTid, 1, __ATOMIC_RELAXED);
  thread->process = proc;
  thread->timeSlice = SCHED_DEFAULT_QUANTUM;
  thread->defaultSlice = SCHED_DEFAULT_QUANTUM;
  thread->state = THREAD_STATE_READY;
  thread->needResched = 0;
  thread->preemptCount = 0;
  thread->userRsp = userRsp;

  *(uint16_t *)&thread->fpuState[0] = 0x037F;
  *(uint32_t *)&thread->fpuState[24] = 0x1F80;

  IretFrame *iret = (IretFrame *)(thread->kstackTop - sizeof(IretFrame));
  iret->rip = entry;
  iret->cs = GDT_USER_CODE_RPL3;
  iret->rflags = 0x202;
  iret->rsp = userRsp;
  iret->ss = GDT_USER_DATA_RPL3;

  Context *ctx = (Context *)((uint64_t)iret - sizeof(Context));
  memset(ctx, 0, sizeof(Context));
  ctx->r12 = arg;
  ctx->rip = (uint64_t)userThreadTrampoline;
  thread->rsp = (uint64_t)ctx;

  uint64_t rflags = spin_lock_irqsave(&gSchedLock);
  thread->next = proc->threads;
  proc->threads = thread;

  enqueueReady(thread);
  spin_unlock_irqrestore(&gSchedLock, rflags);

  return thread;
}

Process *schedSpawn(const char *path, const char *name, const char **argv, const char **envp) {
  if (!path) {
    return NULL;
  }

  void *rawData = NULL;
  size_t fileSize = 0;
  if (fs_read_file(path, &rawData, &fileSize) != 0 || !rawData || fileSize < sizeof(Elf64_Ehdr)) {
    kprintf("[SPAWN] Error: Failed to read executable '%s'\n", path);
    if (rawData) kfree(rawData);
    return NULL;
  }

  const uint8_t *raw = (const uint8_t *)rawData;
  const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)raw;

  if (ehdr->e_ident[0] != ELFMAG0 || ehdr->e_ident[1] != ELFMAG1 || ehdr->e_ident[2] != ELFMAG2 || ehdr->e_ident[3] != ELFMAG3 || ehdr->e_ident[4] != ELFCLASS64 || ehdr->e_ident[5] != ELFDATA2LSB ||
    ehdr->e_machine != EM_X86_64 || (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN)) {
    kfree(rawData);
    return NULL;
  }

  if (ehdr->e_phoff + ((uint64_t)ehdr->e_phnum * ehdr->e_phentsize) > fileSize) {
    kfree(rawData);
    return NULL;
  }

  uint64_t loadBias = (ehdr->e_type == ET_DYN) ? 0x0000000000400000ULL : 0;
  const char *procName = name ? name : path;
  Process *proc = schedCreateProcess(procName);
  if (!proc) {
    kfree(rawData);
    return NULL;
  }

  const Elf64_Phdr *phdrs = (const Elf64_Phdr *)(raw + ehdr->e_phoff);

  for (size_t i = 0; i < ehdr->e_phnum; i++) {
    const Elf64_Phdr *phdr = &phdrs[i];
    if (phdr->p_type != PT_LOAD) {
      continue;
    }

    if (phdr->p_offset + phdr->p_filesz > fileSize || phdr->p_filesz > phdr->p_memsz) {
      vmaDestroyAll(proc);
      vmmDestroyAddressSpace(proc->pml4);
      kfree(proc);
      kfree(rawData);
      return NULL;
    }

    uint64_t vaddr = phdr->p_vaddr + loadBias;
    if (vaddr + phdr->p_memsz > USER_SPACE_MAX) {
      vmaDestroyAll(proc);
      vmmDestroyAddressSpace(proc->pml4);
      kfree(proc);
      kfree(rawData);
      return NULL;
    }

    uint32_t vmaFlags = VMA_USER;
    if (phdr->p_flags & PF_R) vmaFlags |= VMA_READ;
    if (phdr->p_flags & PF_W) vmaFlags |= VMA_WRITE;
    if (phdr->p_flags & PF_X) vmaFlags |= VMA_EXEC;

    uint64_t segStart = vaddr & ~(PAGE_SIZE - 1);
    uint64_t segEnd = (vaddr + phdr->p_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t segSize = segEnd - segStart;

    if (!vmaCreate(proc, segStart, segSize, vmaFlags)) {
      vmaDestroyAll(proc);
      vmmDestroyAddressSpace(proc->pml4);
      kfree(proc);
      kfree(rawData);
      return NULL;
    }

    for (uint64_t pageV = segStart; pageV < segEnd; pageV += PAGE_SIZE) {
      void *frame = pmm_alloc_frame();
      if (!frame) {
        vmaDestroyAll(proc);
        vmmDestroyAddressSpace(proc->pml4);
        kfree(proc);
        kfree(rawData);
        return NULL;
      }

      memset((void *)((uint64_t)frame + HHDM_BASE), 0, PAGE_SIZE);

      uint64_t fileStart = vaddr;
      uint64_t fileEnd = vaddr + phdr->p_filesz;
      uint64_t pageEnd = pageV + PAGE_SIZE;

      uint64_t overlapStart = (pageV > fileStart) ? pageV : fileStart;
      uint64_t overlapEnd = (pageEnd < fileEnd) ? pageEnd : fileEnd;

      if (overlapStart < overlapEnd) {
        uint64_t fileOffset = phdr->p_offset + (overlapStart - fileStart);
        uint64_t frameOffset = overlapStart - pageV;
        size_t copyLen = (size_t)(overlapEnd - overlapStart);
        memcpy((uint8_t *)frame + HHDM_BASE + frameOffset, raw + fileOffset, copyLen);
      }

      uint64_t pteFlags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
      if (phdr->p_flags & PF_W) pteFlags |= VMM_FLAG_WRITABLE;
      if (!(phdr->p_flags & PF_X)) pteFlags |= VMM_FLAG_NO_EXECUTE;

      if (vmmMapPage(proc->pml4, pageV, (uint64_t)frame, pteFlags) != 0) {
        pmm_free_frame(frame);
        vmaDestroyAll(proc);
        vmmDestroyAddressSpace(proc->pml4);
        kfree(proc);
        kfree(rawData);
        return NULL;
      }
    }
  }

  uint64_t stackTop = USER_STACK_TOP_DEFAULT;
  uint64_t stackSize = USER_STACK_INITIAL_SIZE;
  uint64_t stackBase = stackTop - stackSize;

  if (!vmaCreate(proc, stackBase, stackSize, VMA_READ | VMA_WRITE | VMA_USER | VMA_STACK | VMA_ANON)) {
    vmaDestroyAll(proc);
    vmmDestroyAddressSpace(proc->pml4);
    kfree(proc);
    kfree(rawData);
    return NULL;
  }

  uint64_t topPageVaddr = stackTop - PAGE_SIZE;
  void *stackFrame = pmm_alloc_frame();
  if (!stackFrame) {
    vmaDestroyAll(proc);
    vmmDestroyAddressSpace(proc->pml4);
    kfree(proc);
    kfree(rawData);
    return NULL;
  }

  memset((void *)((uint64_t)stackFrame + HHDM_BASE), 0, PAGE_SIZE);
  if (vmmMapPage(proc->pml4, topPageVaddr, (uint64_t)stackFrame, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER) != 0) {
    pmm_free_frame(stackFrame);
    vmaDestroyAll(proc);
    vmmDestroyAddressSpace(proc->pml4);
    kfree(proc);
    kfree(rawData);
    return NULL;
  }

  uint8_t *stackMem = (uint8_t *)stackFrame + HHDM_BASE;
  uint64_t currentUserSp = stackTop;

  const char *defaultArgv[2] = { proc->name, NULL };
  if (!argv) {
    argv = defaultArgv;
  }

  int argc = 0;
  while (argv[argc]) argc++;
  if (argc > 63) argc = 63;

  uint64_t userArgvPtrs[64];

  for (int i = argc - 1; i >= 0; i--) {
    size_t len = strlen(argv[i]) + 1;
    currentUserSp -= len;
    uint64_t pageOffset = currentUserSp - topPageVaddr;
    memcpy(stackMem + pageOffset, argv[i], len);
    userArgvPtrs[i] = currentUserSp;
  }

  currentUserSp &= ~0x7ULL;

  int envc = 0;
  if (envp) {
    while (envp[envc]) envc++;
  }

  int totalQwords = (int)(sizeof(uint64_t) * 2 * 3 / 8) + 1 + envc + 1 + argc + 1;
  if ((totalQwords % 2) == 0) {
    currentUserSp -= 8;
    *(uint64_t *)(stackMem + (currentUserSp - topPageVaddr)) = 0;
  }

  struct { uint64_t type; uint64_t val; } auxv[] = {
    { 9, ehdr->e_entry + loadBias },
    { 6, PAGE_SIZE },
    { 0, 0 }
  };

  size_t auxvCount = sizeof(auxv) / sizeof(auxv[0]);
  for (int i = (int)auxvCount - 1; i >= 0; i--) {
    currentUserSp -= 16;
    uint64_t pageOffset = currentUserSp - topPageVaddr;
    *(uint64_t *)(stackMem + pageOffset) = auxv[i].type;
    *(uint64_t *)(stackMem + pageOffset + 8) = auxv[i].val;
  }

  currentUserSp -= 8;
  *(uint64_t *)(stackMem + (currentUserSp - topPageVaddr)) = 0;

  for (int i = envc - 1; i >= 0; i--) {
    size_t len = strlen(envp[i]) + 1;
    uint64_t strSp = currentUserSp - len;
    memcpy(stackMem + (strSp - topPageVaddr), envp[i], len);
    currentUserSp = strSp & ~0x7ULL;
    currentUserSp -= 8;
    *(uint64_t *)(stackMem + (currentUserSp - topPageVaddr)) = strSp;
  }

  currentUserSp -= 8;
  *(uint64_t *)(stackMem + (currentUserSp - topPageVaddr)) = 0;

  for (int i = argc - 1; i >= 0; i--) {
    currentUserSp -= 8;
    *(uint64_t *)(stackMem + (currentUserSp - topPageVaddr)) = userArgvPtrs[i];
  }

  currentUserSp -= 8;
  *(uint64_t *)(stackMem + (currentUserSp - topPageVaddr)) = (uint64_t)argc;

  uint64_t entryPoint = ehdr->e_entry + loadBias;
  kfree(rawData);

  Thread *thread = schedCreateUserThread(proc, entryPoint, currentUserSp, 0);
  if (!thread) {
    vmaDestroyAll(proc);
    vmmDestroyAddressSpace(proc->pml4);
    kfree(proc);
    return NULL;
  }

  return proc;
}

Process *schedGetKernelProcess(void) {
  return gKernelProcess;
}

Thread *schedCreateThread(Process *proc, void (*entry)(void *), void *arg, int isUser) {
  if (!proc || !entry) {
    return NULL;
  }

  if (isUser) {
    return schedCreateUserThread(proc, (uint64_t)entry, 0, (uint64_t)arg);
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
  thread->tid = __atomic_fetch_add(&gNextTid, 1, __ATOMIC_RELAXED);
  thread->process = proc;
  thread->timeSlice = SCHED_DEFAULT_QUANTUM;
  thread->defaultSlice = SCHED_DEFAULT_QUANTUM;
  thread->state = THREAD_STATE_READY;
  thread->needResched = 0;
  thread->preemptCount = 0;
  thread->userRsp = 0;

  *(uint16_t *)&thread->fpuState[0] = 0x037F;
  *(uint32_t *)&thread->fpuState[24] = 0x1F80;

  Context *ctx = (Context *)(thread->kstackTop - sizeof(Context));
  memset(ctx, 0, sizeof(Context));
  ctx->r12 = (uint64_t)entry;
  ctx->r13 = (uint64_t)arg;
  ctx->rip = (uint64_t)threadEntryTrampoline;
  thread->rsp = (uint64_t)ctx;

  uint64_t rflags = spin_lock_irqsave(&gSchedLock);
  thread->next = proc->threads;
  proc->threads = thread;

  enqueueReady(thread);
  spin_unlock_irqrestore(&gSchedLock, rflags);

  return thread;
}

void schedule(void) {
  uint64_t rflags = spin_lock_irqsave(&gSchedLock);

  Thread *prev = gCurrentThread;
  if (prev && prev->preemptCount > 0) {
    spin_unlock_irqrestore(&gSchedLock, rflags);
    return;
  }

  Thread *next = dequeueReady();
  if (!next) {
    if (prev && prev->state == THREAD_STATE_RUNNING) {
      next = prev;
    } else {
      next = gIdleThread;
    }
  }

  if (next == prev) {
    if (prev) {
      prev->needResched = 0;
    }
    spin_unlock_irqrestore(&gSchedLock, rflags);
    return;
  }

  if (prev) {
    if (prev->state == THREAD_STATE_RUNNING) {
      prev->state = THREAD_STATE_READY;
      if (prev != gIdleThread) {
        enqueueReady(prev);
      }
    }
    __asm__ volatile("fxsave64 %0" : "=m"(prev->fpuState));
  }

  next->state = THREAD_STATE_RUNNING;
  next->needResched = 0;
  next->timeSlice = next->defaultSlice;
  gCurrentThread = next;

  if (next->process && next->process->pml4) {
    if (!prev || !prev->process || prev->process != next->process) {
      vmmSwitchPml4(next->process->pml4);
    }
  }

  tss_set_rsp0(next->kstackTop);
  PerCpu *cpu = perCpuGet();
  cpu->kstackTop = next->kstackTop;
  cpu->currentThread = next;
  __asm__ volatile("fxrstor64 %0" : : "m"(next->fpuState));

  cpuSwitchTo(prev, next);

  spin_unlock_irqrestore(&gSchedLock, rflags);
}

void schedYield(void) {
  if (gCurrentThread) {
    gCurrentThread->needResched = 1;
  }
  schedule();
}

void schedSleep(uint64_t ticks) {
  if (!gCurrentThread || ticks == 0) {
    return;
  }

  uint64_t rflags = spin_lock_irqsave(&gSchedLock);
  gCurrentThread->sleepTargetTicks = gSchedTicks + ticks;
  gCurrentThread->state = THREAD_STATE_SLEEPING;

  gCurrentThread->next = gSleepQueueHead;
  gCurrentThread->prev = NULL;
  if (gSleepQueueHead) {
    gSleepQueueHead->prev = gCurrentThread;
  }
  gSleepQueueHead = gCurrentThread;

  spin_unlock_irqrestore(&gSchedLock, rflags);
  schedule();
}

void schedTick(void) {
  uint64_t rflags = spin_lock_irqsave(&gSchedLock);
  gSchedTicks++;

  Thread *currSleep = gSleepQueueHead;
  while (currSleep) {
    Thread *nextSleep = currSleep->next;
    if (gSchedTicks >= currSleep->sleepTargetTicks) {
      if (currSleep->prev) {
        currSleep->prev->next = currSleep->next;
      } else {
        gSleepQueueHead = currSleep->next;
      }
      if (currSleep->next) {
        currSleep->next->prev = currSleep->prev;
      }

      currSleep->next = NULL;
      currSleep->prev = NULL;
      enqueueReady(currSleep);

      if (gCurrentThread) {
        gCurrentThread->needResched = 1;
      }
    }
    currSleep = nextSleep;
  }

  if (gCurrentThread) {
    if (gCurrentThread == gIdleThread) {
      if (gReadyQueueHead) {
        gCurrentThread->needResched = 1;
      }
    } else if (gCurrentThread->timeSlice > 0) {
      gCurrentThread->timeSlice--;
      if (gCurrentThread->timeSlice == 0) {
        gCurrentThread->needResched = 1;
      }
    }
  }

  spin_unlock_irqrestore(&gSchedLock, rflags);
}

void schedPreemptFromInterrupt(const Registers *regs) {
  Thread *currentThread = schedCurrent();
  if (!currentThread || currentThread->preemptCount > 0 || !currentThread->needResched) {
    return;
  }

  if (regs && !(regs->rflags & 0x200)) {
    return;
  }

  schedule();
}

void schedThreadExit(void) {
  uint64_t rflags = spin_lock_irqsave(&gSchedLock);
  if (gCurrentThread) {
    gCurrentThread->state = THREAD_STATE_ZOMBIE;
  }
  spin_unlock_irqrestore(&gSchedLock, rflags);

  schedule();

  while (1) {
    __asm__ volatile("pause; hlt");
  }
}

void schedInit(void) {
  gKernelProcess = schedCreateProcess("kernel");

  gKernelThread = (Thread *)kzalloc(sizeof(Thread));
  gKernelThread->tid = __atomic_fetch_add(&gNextTid, 1, __ATOMIC_RELAXED);
  gKernelThread->state = THREAD_STATE_RUNNING;
  gKernelThread->process = gKernelProcess;
  gKernelThread->timeSlice = SCHED_DEFAULT_QUANTUM;
  gKernelThread->defaultSlice = SCHED_DEFAULT_QUANTUM;
  gKernelThread->preemptCount = 0;
  gKernelThread->needResched = 0;

  gKernelProcess->threads = gKernelThread;
  gCurrentThread = gKernelThread;

  gIdleThread = schedCreateThread(gKernelProcess, idleTask, NULL, 0);

  uint64_t rflags = spin_lock_irqsave(&gSchedLock);
  if (gReadyQueueHead == gIdleThread) {
    gReadyQueueHead = gIdleThread->next;
    if (gReadyQueueHead) {
      gReadyQueueHead->prev = NULL;
    } else {
      gReadyQueueTail = NULL;
    }
  } else if (gIdleThread->prev) {
    gIdleThread->prev->next = gIdleThread->next;
    if (gIdleThread->next) {
      gIdleThread->next->prev = gIdleThread->prev;
    } else {
      gReadyQueueTail = gIdleThread->prev;
    }
  }
  gIdleThread->next = NULL;
  gIdleThread->prev = NULL;
  gIdleThread->state = THREAD_STATE_READY;
  spin_unlock_irqrestore(&gSchedLock, rflags);
}
