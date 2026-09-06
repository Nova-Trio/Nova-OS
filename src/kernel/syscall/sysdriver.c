#include "syscall.h"
#include <driver.h>
#include <vmm.h>
#include <sched.h>
#include <heap.h>

int64_t sysDriverOpen(const char *name) {
  if (!name) {
    return -EFAULT;
  }

  Thread *curr = schedCurrent();
  if (!curr || !curr->process) {
    return -EPERM;
  }

  char nameBuf[256];
  size_t pos = 0;
  int foundNull = 0;

  while (pos < sizeof(nameBuf) - 1) {
    uint64_t addr = (uint64_t)name + pos;
    uint64_t bytesLeftInPage = PAGE_SIZE - (addr & (PAGE_SIZE - 1));
    size_t step = sizeof(nameBuf) - 1 - pos;
    if (step > bytesLeftInPage) {
      step = bytesLeftInPage;
    }

    if (copy_from_user(nameBuf + pos, (const void *)addr, step) != 0) {
      return -EFAULT;
    }

    for (size_t j = 0; j < step; j++) {
      if (nameBuf[pos + j] == '\0') {
        foundNull = 1;
        break;
      }
    }

    if (foundNull) {
      break;
    }

    pos += step;
  }

  if (!foundNull) {
    return -EINVAL;
  }

  return (int64_t)driverOpen(curr->process, nameBuf);
}

int64_t sysDriverIoctl(int handle, uint32_t cmd, void *arg, size_t argSize) {
  Thread *curr = schedCurrent();
  if (!curr || !curr->process) {
    return -EPERM;
  }

  if (argSize == 0 || !arg) {
    return driverIoctl(curr->process, handle, cmd, NULL, 0);
  }

  if (!validate_user_range(arg, argSize, 1)) {
    return -EFAULT;
  }

  uint8_t stackBuf[256];
  void *kbuf = stackBuf;

  if (argSize > sizeof(stackBuf)) {
    kbuf = kmalloc(argSize);
    if (!kbuf) {
      return -ENOMEM;
    }
  }

  if (copy_from_user(kbuf, arg, argSize) != 0) {
    if (kbuf != stackBuf) {
      kfree(kbuf);
    }
    return -EFAULT;
  }

  int64_t ret = driverIoctl(curr->process, handle, cmd, kbuf, argSize);

  if (ret >= 0) {
    if (copy_to_user(arg, kbuf, argSize) != 0) {
      ret = -EFAULT;
    }
  }

  if (kbuf != stackBuf) {
    kfree(kbuf);
  }

  return ret;
}

int64_t sysDriverClose(int handle) {
  Thread *curr = schedCurrent();
  if (!curr || !curr->process) {
    return -EPERM;
  }

  return (int64_t)driverClose(curr->process, handle);
}
