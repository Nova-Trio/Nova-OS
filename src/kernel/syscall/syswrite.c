#include "syscall.h"
#include <vmm.h>
#include <console.h>

int64_t sysWrite(int Fd, const void *Buf, size_t Count) {
  if (Count == 0) {
    return 0;
  }
  if (Fd != 1 && Fd != 2) {
    return -EBADF;
  }
  if (!validate_user_range(Buf, Count, 0)) {
    return -EFAULT;
  }

  char Chunk[256];
  const uint8_t *UserBuf = (const uint8_t *)Buf;
  size_t Written = 0;

  while (Written < Count) {
    size_t Step = Count - Written;
    if (Step >= sizeof(Chunk)) {
      Step = sizeof(Chunk) - 1;
    }

    if (copy_from_user(Chunk, UserBuf + Written, Step) != 0) {
      return Written > 0 ? (int64_t)Written : -EFAULT;
    }

    Chunk[Step] = '\0';
    kprintf("%s", Chunk);

    Written += Step;
  }

  return (int64_t)Written;
}
