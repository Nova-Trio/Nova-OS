#include "syscall.h"
#include <vmm.h>
#include <pmm.h>
#include <sched.h>
#include <fat32.h>
#include <string.h>

void *sysMmap(void *Addr, size_t Length, int Prot, int Flags, int Fd, int64_t Offset) {
  if (Length == 0) {
    return (void *)-EINVAL;
  }

  if (Offset < 0 || (Offset & (PAGE_SIZE - 1)) != 0) {
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

  FileHandle *fh = NULL;
  if (!(Flags & MAP_ANONYMOUS)) {
    if (Fd < 3) {
      return (void *)-EBADF;
    }

    uint64_t fl = spin_lock_irqsave(&Proc->fileTable.lock);
    if ((size_t)Fd >= Proc->fileTable.capacity || !Proc->fileTable.handles[Fd]) {
      spin_unlock_irqrestore(&Proc->fileTable.lock, fl);
      return (void *)-EBADF;
    }

    fh = Proc->fileTable.handles[Fd];
    if (fh->isDir) {
      spin_unlock_irqrestore(&Proc->fileTable.lock, fl);
      return (void *)-EACCES;
    }
    spin_unlock_irqrestore(&Proc->fileTable.lock, fl);
  }

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

  if (!(Flags & MAP_ANONYMOUS) && fh) {
    uint64_t pteFlags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
    if (Prot & PROT_WRITE) pteFlags |= VMM_FLAG_WRITABLE;
    if (!(Prot & PROT_EXEC)) pteFlags |= VMM_FLAG_NO_EXECUTE;

    for (uint64_t pageOff = 0; pageOff < AlignedLen; pageOff += PAGE_SIZE) {
      void *frame = pmm_alloc_frame();
      if (!frame) {
        vma_destroy(Proc, TargetAddr, AlignedLen);
        return (void *)-ENOMEM;
      }

      uint8_t *framePtr = (uint8_t *)frame + HHDM_BASE;
      memset(framePtr, 0, PAGE_SIZE);

      uint64_t currentFilePos = (uint64_t)Offset + pageOff;
      if (currentFilePos < fh->size) {
        size_t bytesToRead = PAGE_SIZE;
        if (currentFilePos + bytesToRead > fh->size) {
          bytesToRead = (size_t)(fh->size - currentFilePos);
        }

        size_t actualRead = 0;
        fs_read(fh->path, currentFilePos, bytesToRead, framePtr, &actualRead);
      }

      uint64_t targetPage = TargetAddr + pageOff;
      if (vmmMapPage(Proc->pml4, targetPage, (uint64_t)frame, pteFlags) != 0) {
        pmm_free_frame(frame);
        vma_destroy(Proc, TargetAddr, AlignedLen);
        return (void *)-ENOMEM;
      }
    }
  }

  return (void *)TargetAddr;
}
