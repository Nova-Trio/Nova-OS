#include "syscall.h"
#include <vmm.h>
#include <sched.h>

void *sysMmap(void *Addr, size_t Length, int Prot, int Flags, int Fd, int64_t Offset) {
  (void)Fd;
  (void)Offset;

  if (Length == 0) {
    return (void *)-EINVAL;
  }

  uint64_t AlignedLen = (Length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  if (AlignedLen == 0 || AlignedLen > (USER_SPACE_MAX / 2)) {
    return (void *)-ENOMEM;
  }

  Thread *Curr = schedCurrent();
  if (!Curr || !Curr->process) {
    return (void *)-EPERM;
  }
  Process *Proc = Curr->process;

  uint32_t VmaFlags = VMA_USER;
  if (Prot & PROT_READ)  VmaFlags |= VMA_READ;
  if (Prot & PROT_WRITE) VmaFlags |= VMA_WRITE;
  if (Prot & PROT_EXEC)  VmaFlags |= VMA_EXEC;
  if (Flags & MAP_ANONYMOUS) VmaFlags |= VMA_ANON;

  uint64_t TargetAddr = (uint64_t)Addr & ~(PAGE_SIZE - 1);
  uint64_t Rflags = spin_lock_irqsave(&Proc->vma_lock);

  if ((Flags & MAP_FIXED) && TargetAddr != 0) {
    if (TargetAddr + AlignedLen > USER_SPACE_MAX) {
      spin_unlock_irqrestore(&Proc->vma_lock, Rflags);
      return (void *)-EINVAL;
    }

    spin_unlock_irqrestore(&Proc->vma_lock, Rflags);
    vma_destroy(Proc, TargetAddr, AlignedLen);
    Rflags = spin_lock_irqsave(&Proc->vma_lock);
  } else {
    TargetAddr = USER_MMAP_BASE;

    while (TargetAddr + AlignedLen < USER_STACK_TOP_DEFAULT - USER_STACK_MAX_SIZE) {
      int Collision = 0;
      for (Vma *V = Proc->vma_head; V; V = V->next) {
        if (TargetAddr < V->end && (TargetAddr + AlignedLen) > V->start) {
          TargetAddr = (V->end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
          Collision = 1;
          break;
        }
      }
      if (!Collision) {
        break;
      }
    }

    if (TargetAddr + AlignedLen >= USER_STACK_TOP_DEFAULT - USER_STACK_MAX_SIZE) {
      spin_unlock_irqrestore(&Proc->vma_lock, Rflags);
      return (void *)-ENOMEM;
    }
  }

  spin_unlock_irqrestore(&Proc->vma_lock, Rflags);

  Vma *AllocatedVma = vma_create(Proc, TargetAddr, AlignedLen, VmaFlags);
  if (!AllocatedVma) {
    return (void *)-ENOMEM;
  }

  return (void *)TargetAddr;
}
