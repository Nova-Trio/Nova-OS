#include <vmm.h>
#include <bootinfo.h>
#include <pmm.h>
#include <heap.h>
#include <sched.h>
#include <idt.h>
#include <console.h>
#include <string.h>
#include <stdint.h>

#define MSR_IA32_PAT 0x277
#define PAT_VALUE 0x0007050600070106ULL

static inline void wrmsr(uint32_t msr, uint64_t val){
  uint32_t low = (uint32_t)val;
  uint32_t hig = (uint32_t)(val >> 32);
  __asm__ volatile("wrmsr" : : "a"(low), "d"(hig), "c"(msr) : "memory");
}

void vmmInitPat(void){
  wrmsr(MSR_IA32_PAT, PAT_VALUE);
}

#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

static PageDirectory gKernelPml4 = NULL;

static uint64_t *getOrAllocTable(uint64_t *entry, uint64_t flags) {
  if (*entry & VMM_FLAG_PRESENT) {
    if (flags & VMM_FLAG_USER) {
      *entry |= VMM_FLAG_USER;
    }
    if (flags & VMM_FLAG_WRITABLE) {
      *entry |= VMM_FLAG_WRITABLE;
    }
    return (uint64_t *)((*entry & PTE_ADDR_MASK) + HHDM_BASE);
  }

  void *frame = pmm_alloc_frame();
  if (!frame) {
    return NULL;
  }

  uint64_t *table = (uint64_t *)((uint64_t)frame + HHDM_BASE);
  for (size_t i = 0; i < 512; i++) {
    table[i] = 0;
  }

  uint64_t entryFlags = VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE;
  if (flags & VMM_FLAG_USER) {
    entryFlags |= VMM_FLAG_USER;
  }

  *entry = (uint64_t)frame | entryFlags;
  return table;
}

void vmmInit(BootInfo *bootInfo) {
  vmmInitPat();
  void *newPml4Phys = pmm_alloc_frame();
  if (!newPml4Phys) {
    return;
  }

  gKernelPml4 = (PageDirectory)((uint64_t)newPml4Phys + HHDM_BASE);

  for (size_t i = 0; i < 512; i++) {
    gKernelPml4[i] = 0;
  }

  for (size_t i = 256; i < 512; i++) {
    gKernelPml4[i] = bootInfo->pml4[i];
  }

  vmmSwitchPml4(gKernelPml4);
  bootInfo->pml4 = gKernelPml4;

  idt_register_handler(14, vmmPageFaultHandler);
}

int vmmMapPage(PageDirectory pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
  virt &= ~(PAGE_SIZE - 1);
  phys &= ~(PAGE_SIZE - 1);

  size_t pml4Idx = (virt >> 39) & 0x1FF;
  size_t pdptIdx = (virt >> 30) & 0x1FF;
  size_t pdIdx = (virt >> 21) & 0x1FF;
  size_t ptIdx = (virt >> 12) & 0x1FF;

  uint64_t *pdpt = getOrAllocTable(&pml4[pml4Idx], flags);
  if (!pdpt) {
    return -1;
  }

  uint64_t *pd = getOrAllocTable(&pdpt[pdptIdx], flags);
  if (!pd) {
    return -1;
  }

  uint64_t *pt = getOrAllocTable(&pd[pdIdx], flags);
  if (!pt) {
    return -1;
  }

  pt[ptIdx] = (phys & PTE_ADDR_MASK) | flags | VMM_FLAG_PRESENT;
  vmmInvlpg(virt);
  return 0;
}

static inline int isTableEmpty(const uint64_t *table) {
  for (size_t i = 0; i < 512; i++) {
    if (table[i] & VMM_FLAG_PRESENT) {
      return 0;
    }
  }
  return 1;
}

int vmmUnmapPage(PageDirectory pml4, uint64_t virt) {
  virt &= ~(PAGE_SIZE - 1);

  size_t pml4Idx = (virt >> 39) & 0x1FF;
  size_t pdptIdx = (virt >> 30) & 0x1FF;
  size_t pdIdx   = (virt >> 21) & 0x1FF;
  size_t ptIdx   = (virt >> 12) & 0x1FF;

  if (!(pml4[pml4Idx] & VMM_FLAG_PRESENT)) {
    return 0;
  }

  uint64_t pdptPhys = pml4[pml4Idx] & PTE_ADDR_MASK;
  uint64_t *pdpt = (uint64_t *)(pdptPhys + HHDM_BASE);
  if (!(pdpt[pdptIdx] & VMM_FLAG_PRESENT)) {
    return 0;
  }

  if (pdpt[pdptIdx] & VMM_FLAG_HUGE) {
    return 0;
  }

  uint64_t pdPhys = pdpt[pdptIdx] & PTE_ADDR_MASK;
  uint64_t *pd = (uint64_t *)(pdPhys + HHDM_BASE);
  if (!(pd[pdIdx] & VMM_FLAG_PRESENT)) {
    return 0;
  }

  if (pd[pdIdx] & VMM_FLAG_HUGE) {
    pd[pdIdx] = 0;
    vmmInvlpg(virt);
    return 0;
  }

  uint64_t ptPhys = pd[pdIdx] & PTE_ADDR_MASK;
  uint64_t *pt = (uint64_t *)(ptPhys + HHDM_BASE);

  pt[ptIdx] = 0;
  vmmInvlpg(virt);

  if (isTableEmpty(pt)) {
    pd[pdIdx] = 0;
    pmm_free_frame((void *)ptPhys);

    if (isTableEmpty(pd)) {
      pdpt[pdptIdx] = 0;
      pmm_free_frame((void *)pdPhys);

      if (pml4Idx < 256 && isTableEmpty(pdpt)) {
        pml4[pml4Idx] = 0;
        pmm_free_frame((void *)pdptPhys);
      }
    }
  }

  return 0;
}

int vmmMapRange(PageDirectory pml4, uint64_t virtStart, uint64_t physStart, uint64_t size, uint64_t flags) {
  uint64_t alignedSize = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

  for (uint64_t offset = 0; offset < alignedSize; offset += PAGE_SIZE) {
    if (vmmMapPage(pml4, virtStart + offset, physStart + offset, flags) != 0) {
      return -1;
    }
  }

  return 0;
}

int vmmUnmapRange(PageDirectory pml4, uint64_t virtStart, uint64_t size) {
  uint64_t alignedSize = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

  for (uint64_t offset = 0; offset < alignedSize; offset += PAGE_SIZE) {
    vmmUnmapPage(pml4, virtStart + offset);
  }

  return 0;
}

uint64_t vmmVirtToPhys(PageDirectory pml4, uint64_t virt) {
  size_t pml4Idx = (virt >> 39) & 0x1FF;
  size_t pdptIdx = (virt >> 30) & 0x1FF;
  size_t pdIdx = (virt >> 21) & 0x1FF;
  size_t ptIdx = (virt >> 12) & 0x1FF;
  uint64_t offset = virt & (PAGE_SIZE - 1);

  if (!(pml4[pml4Idx] & VMM_FLAG_PRESENT)) {
    return 0;
  }

  uint64_t *pdpt = (uint64_t *)((pml4[pml4Idx] & PTE_ADDR_MASK) + HHDM_BASE);
  if (!(pdpt[pdptIdx] & VMM_FLAG_PRESENT)) {
    return 0;
  }

  if (pdpt[pdptIdx] & VMM_FLAG_HUGE) {
    return (pdpt[pdptIdx] & 0x000FFFFFC0000000ULL) | (virt & 0x3FFFFFFFULL);
  }

  uint64_t *pd = (uint64_t *)((pdpt[pdptIdx] & PTE_ADDR_MASK) + HHDM_BASE);
  if (!(pd[pdIdx] & VMM_FLAG_PRESENT)) {
    return 0;
  }

  if (pd[pdIdx] & VMM_FLAG_HUGE) {
    return (pd[pdIdx] & 0x000FFFFFFFE00000ULL) | (virt & 0x1FFFFFULL);
  }

  uint64_t *pt = (uint64_t *)((pd[pdIdx] & PTE_ADDR_MASK) + HHDM_BASE);
  if (!(pt[ptIdx] & VMM_FLAG_PRESENT)) {
    return 0;
  }

  return (pt[ptIdx] & PTE_ADDR_MASK) | offset;
}

PageDirectory vmmGetKernelPml4(void) {
  return gKernelPml4;
}

void vmmSwitchPml4(PageDirectory pml4) {
  uint64_t pml4Phys = (uint64_t)pml4 - HHDM_BASE;
  __asm__ volatile("mov %0, %%cr3" : : "r"(pml4Phys) : "memory");
}

PageDirectory vmmCreateAddressSpace(void) {
  void *frame = pmm_alloc_frame();
  if (!frame) return NULL;

  PageDirectory pml4 = (PageDirectory)((uint64_t)frame + HHDM_BASE);

  for (size_t i = 0; i < 256; i++) {
    pml4[i] = 0;
  }

  for (size_t i = 256; i < 512; i++) {
    pml4[i] = gKernelPml4[i];
  }

  return pml4;
}

Vma *vmaCreate(Process *proc, uint64_t start, uint64_t size, uint32_t flags) {
  if (!proc || size == 0) {
    return NULL;
  }

  uint64_t pageStart = start & ~(PAGE_SIZE - 1);
  uint64_t pageEnd = (start + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

  if (pageStart >= pageEnd || pageEnd > USER_SPACE_MAX) {
    return NULL;
  }

  uint64_t rflags = spin_lock_irqsave(&proc->vmaLock);

  for (Vma *curr = proc->vmaHead; curr; curr = curr->next) {
    if (pageStart < curr->end && pageEnd > curr->start) {
      spin_unlock_irqrestore(&proc->vmaLock, rflags);
      return NULL;
    }
  }

  Vma *vma = (Vma *)kmalloc(sizeof(Vma));
  if (!vma) {
    spin_unlock_irqrestore(&proc->vmaLock, rflags);
    return NULL;
  }

  vma->start = pageStart;
  vma->end = pageEnd;
  vma->flags = flags;
  vma->next = NULL;
  vma->prev = NULL;

  if (!proc->vmaHead || pageStart < proc->vmaHead->start) {
    vma->next = proc->vmaHead;
    if (proc->vmaHead) {
      proc->vmaHead->prev = vma;
    }
    proc->vmaHead = vma;
  } else {
    Vma *it = proc->vmaHead;
    while (it->next && it->next->start < pageStart) {
      it = it->next;
    }
    vma->next = it->next;
    vma->prev = it;
    if (it->next) {
      it->next->prev = vma;
    }
    it->next = vma;
  }

  spin_unlock_irqrestore(&proc->vmaLock, rflags);
  return vma;
}

int vmaDestroy(Process *proc, uint64_t start, uint64_t size) {
  if (!proc || size == 0) {
    return -1;
  }

  uint64_t pageStart = start & ~(PAGE_SIZE - 1);
  uint64_t pageEnd = (start + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

  uint64_t rflags = spin_lock_irqsave(&proc->vmaLock);

  Vma *curr = proc->vmaHead;
  while (curr) {
    Vma *next = curr->next;

    if (curr->end > pageStart && curr->start < pageEnd) {
      uint64_t unmapStart = (curr->start > pageStart) ? curr->start : pageStart;
      uint64_t unmapEnd = (curr->end < pageEnd) ? curr->end : pageEnd;

      for (uint64_t addr = unmapStart; addr < unmapEnd; addr += PAGE_SIZE) {
        uint64_t phys = vmmVirtToPhys(proc->pml4, addr);
        if (phys) {
          vmmUnmapPage(proc->pml4, addr);
          pmm_free_frame((void *)phys);
        }
      }

      if (curr->start >= pageStart && curr->end <= pageEnd) {
        if (curr->prev) {
          curr->prev->next = curr->next;
        } else {
          proc->vmaHead = curr->next;
        }
        if (curr->next) {
          curr->next->prev = curr->prev;
        }
        kfree(curr);
      } else if (curr->start < pageStart && curr->end > pageEnd) {
        Vma *right = (Vma *)kmalloc(sizeof(Vma));
        if (right) {
          right->start = pageEnd;
          right->end = curr->end;
          right->flags = curr->flags;
          right->next = curr->next;
          right->prev = curr;
          if (curr->next) {
            curr->next->prev = right;
          }
          curr->next = right;
        }
        curr->end = pageStart;
      } else if (curr->start < pageStart) {
        curr->end = pageStart;
      } else {
        curr->start = pageEnd;
      }
    }

    curr = next;
  }

  spin_unlock_irqrestore(&proc->vmaLock, rflags);
  return 0;
}

Vma *vmaFind(Process *proc, uint64_t addr) {
  if (!proc) return NULL;
  for (Vma *curr = proc->vmaHead; curr; curr = curr->next) {
    if (addr >= curr->start && addr < curr->end) {
      return curr;
    }
  }
  return NULL;
}

int validateUserRange(const void *userPtr, size_t size, int write) {
  if (size == 0) {
    return 1;
  }
  if (!userPtr) {
    return 0;
  }

  uint64_t start = (uint64_t)userPtr;
  uint64_t end = start + size;

  if (end < start || end > USER_SPACE_MAX) {
    return 0;
  }

  Thread *curr = schedCurrent();
  if (!curr || !curr->process) {
    return 0;
  }

  Process *proc = curr->process;
  uint64_t pageStart = start & ~(PAGE_SIZE - 1);
  uint64_t pageEnd = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

  uint64_t rflags = spin_lock_irqsave(&proc->vmaLock);

  for (uint64_t cur = pageStart; cur < pageEnd; ) {
    Vma *vma = vmaFind(proc, cur);
    if (!vma) {
      spin_unlock_irqrestore(&proc->vmaLock, rflags);
      return 0;
    }

    if (!(vma->flags & VMA_USER)) {
      spin_unlock_irqrestore(&proc->vmaLock, rflags);
      return 0;
    }

    if (write && !(vma->flags & VMA_WRITE)) {
      spin_unlock_irqrestore(&proc->vmaLock, rflags);
      return 0;
    }

    if (!write && !(vma->flags & VMA_READ)) {
      spin_unlock_irqrestore(&proc->vmaLock, rflags);
      return 0;
    }

    cur = vma->end;
  }

  spin_unlock_irqrestore(&proc->vmaLock, rflags);
  return 1;
}

int copyFromUser(void *dst, const void *src, size_t n) {
  if (n == 0) {
    return 0;
  }
  if (!validateUserRange(src, n, 0)) {
    return -14;
  }
  memcpy(dst, src, n);
  return 0;
}

int copyToUser(void *dst, const void *src, size_t n) {
  if (n == 0) {
    return 0;
  }
  if (!validateUserRange(dst, n, 1)) {
    return -14;
  }
  memcpy(dst, src, n);
  return 0;
}

void vmaDestroyAll(Process *proc) {
  if (!proc) return;

  uint64_t rflags = spin_lock_irqsave(&proc->vmaLock);
  Vma *curr = proc->vmaHead;
  while (curr) {
    Vma *next = curr->next;
    kfree(curr);
    curr = next;
  }
  proc->vmaHead = NULL;
  spin_unlock_irqrestore(&proc->vmaLock, rflags);
}

void vmmPageFaultHandler(Registers *regs) {
  uint64_t cr2;
  __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

  int isUserMode = (regs->cs & 3) == 3;
  int isPresent = (regs->error_code & 1) != 0;
  int isWrite = (regs->error_code & 2) != 0;
  int isInstructionFetch = (regs->error_code & 16) != 0;

  Thread *currThread = schedCurrent();
  Process *currProc = currThread ? currThread->process : NULL;

  if (!currProc || cr2 >= USER_SPACE_MAX) {
    default_exception_handler(regs);
    return;
  }

  uint64_t rflags = spin_lock_irqsave(&currProc->vmaLock);
  Vma *vma = vmaFind(currProc, cr2);

  if (!vma) {
    for (Vma *it = currProc->vmaHead; it; it = it->next) {
      if (it->flags & VMA_STACK) {
        if (cr2 < it->start && (it->end - (cr2 & ~(PAGE_SIZE - 1))) <= USER_STACK_MAX_SIZE) {
          if (cr2 + 256 >= regs->rsp) {
            it->start = cr2 & ~(PAGE_SIZE - 1);
            vma = it;
            break;
          }
        }
      }
    }
  }

  if (!vma) {
    spin_unlock_irqrestore(&currProc->vmaLock, rflags);
    if (isUserMode) {
      kprintf("[SEGV] Process \"%s\" (PID %u) unmapped memory fault at %p (RIP: %p, Error: 0x%llx)\n", currProc->name, currProc->pid, (void *)cr2, (void *)regs->rip, regs->error_code);
      schedThreadExit();
    }
    default_exception_handler(regs);
    return;
  }

  if (isPresent || (isWrite && !(vma->flags & VMA_WRITE)) || (isInstructionFetch && !(vma->flags & VMA_EXEC)) || (isUserMode && !(vma->flags & VMA_USER))) {
    spin_unlock_irqrestore(&currProc->vmaLock, rflags);
    if (isUserMode) {
      kprintf("[SEGV] Process \"%s\" (PID %u) permission violation at %p (RIP: %p, Error: 0x%llx)\n", currProc->name, currProc->pid, (void *)cr2, (void *)regs->rip, regs->error_code);
      schedThreadExit();
    }
    default_exception_handler(regs);
    return;
  }

  void *frame = pmm_alloc_frame();
  if (!frame) {
    spin_unlock_irqrestore(&currProc->vmaLock, rflags);
    kprintf("[VMM] Error: Out of memory servicing page fault for PID %u\n", currProc->pid);
    if (isUserMode) {
      schedThreadExit();
    }
    default_exception_handler(regs);
    return;
  }

  memset((void *)((uint64_t)frame + HHDM_BASE), 0, PAGE_SIZE);

  uint64_t pageAddr = cr2 & ~(PAGE_SIZE - 1);
  uint64_t mapFlags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
  if (vma->flags & VMA_WRITE) {
    mapFlags |= VMM_FLAG_WRITABLE;
  }
  if (!(vma->flags & VMA_EXEC)) {
    mapFlags |= VMM_FLAG_NO_EXECUTE;
  }

  if (vmmMapPage(currProc->pml4, pageAddr, (uint64_t)frame, mapFlags) != 0) {
    pmm_free_frame(frame);
    spin_unlock_irqrestore(&currProc->vmaLock, rflags);
    if (isUserMode) {
      schedThreadExit();
    }
    default_exception_handler(regs);
    return;
  }

  spin_unlock_irqrestore(&currProc->vmaLock, rflags);
}

void vmmDestroyAddressSpace(PageDirectory pml4) {
  if (!pml4 || pml4 == gKernelPml4) {
    return;
  }

  for (size_t pml4Idx = 0; pml4Idx < 256; pml4Idx++) {
    if (!(pml4[pml4Idx] & VMM_FLAG_PRESENT)) {
      continue;
    }

    uint64_t pdptPhys = pml4[pml4Idx] & PTE_ADDR_MASK;
    uint64_t *pdpt = (uint64_t *)(pdptPhys + HHDM_BASE);

    for (size_t pdptIdx = 0; pdptIdx < 512; pdptIdx++) {
      if (!(pdpt[pdptIdx] & VMM_FLAG_PRESENT) || (pdpt[pdptIdx] & VMM_FLAG_HUGE)) {
        continue;
      }

      uint64_t pdPhys = pdpt[pdptIdx] & PTE_ADDR_MASK;
      uint64_t *pd = (uint64_t *)(pdPhys + HHDM_BASE);

      for (size_t pdIdx = 0; pdIdx < 512; pdIdx++) {
        if (!(pd[pdIdx] & VMM_FLAG_PRESENT) || (pd[pdIdx] & VMM_FLAG_HUGE)) {
          continue;
        }

        uint64_t ptPhys = pd[pdIdx] & PTE_ADDR_MASK;
        uint64_t *pt = (uint64_t *)(ptPhys + HHDM_BASE);

        for (size_t ptIdx = 0; ptIdx < 512; ptIdx++) {
          if (pt[ptIdx] & VMM_FLAG_PRESENT) {
            uint64_t frame = pt[ptIdx] & PTE_ADDR_MASK;
            pmm_free_frame((void *)frame);
          }
        }
        pmm_free_frame((void *)ptPhys);
      }
      pmm_free_frame((void *)pdPhys);
    }
    pmm_free_frame((void *)pdptPhys);
  }

  uint64_t pml4Phys = (uint64_t)pml4 - HHDM_BASE;
  pmm_free_frame((void *)pml4Phys);
}
