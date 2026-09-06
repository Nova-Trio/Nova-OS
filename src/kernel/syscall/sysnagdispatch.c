#include "syscall.h"
#include "vmm.h"
#include <nag.h>
#include <heap.h>
#include <stdint.h>

int64_t sysNagDispatch(uint32_t adapterId, uint32_t op, void *arg, size_t argSize) {
  if(argSize == 0 || !arg){
    return nagDispatch(adapterId, (NagOp)op, NULL, 0);
  }

  if(!validateUserRange(arg, argSize, 1)){
    return -EFAULT;
  }

  uint8_t stackBuf[256];
  void* kbuf = stackBuf;

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

  int ret = nagDispatch(adapterId, (NagOp)op, kbuf, argSize);

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
