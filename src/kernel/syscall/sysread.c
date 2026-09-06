#include "syscall.h"
#include <vmm.h>

int64_t sysRead(int Fd, void *Buf, size_t Count) {
  (void)Fd;
  (void)Buf;
  (void)Count;
  return 0;
}
