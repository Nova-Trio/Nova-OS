#include "syscall.h"
#include <vmm.h>
#include <sched.h>

int64_t sysMunmap(void *Addr, size_t Length) {
  if ((uint64_t)Addr & (PAGE_SIZE - 1)) {
    return -EINVAL;
  }
  if (Length == 0) {
    return -EINVAL;
  }

  Thread *Curr = schedCurrent();
  if (!Curr || !Curr->process) {
    return -EPERM;
  }

  uint64_t AlignedLen = (Length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  if ((uint64_t)Addr + AlignedLen > USER_SPACE_MAX) {
    return -EINVAL;
  }

  if (vma_destroy(Curr->process, (uint64_t)Addr, AlignedLen) != 0) {
    return -EINVAL;
  }

  return 0;
}
