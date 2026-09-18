#include "syscall.h"
#include <vmm.h>
#include <sched.h>
#include <heap.h>
#include <fat32.h>

#define FILE_IO_CHUNK 65536

int64_t sysRead(int Fd, void *Buf, size_t Count) {
  if (Count == 0) {
    return 0;
  }
  if (Fd == 0) {
    return 0;
  }
  if (Fd < 0 || Fd == 1 || Fd == 2) {
    return -EBADF;
  }
  if (!validate_user_range(Buf, Count, 1)) {
    return -EFAULT;
  }

  Thread *curr = schedCurrent();
  if (!curr || !curr->process) {
    return -EPERM;
  }
  Process *proc = curr->process;

  uint64_t rflags = spin_lock_irqsave(&proc->fileTable.lock);
  if ((size_t)Fd >= proc->fileTable.capacity || !proc->fileTable.handles[Fd]) {
    spin_unlock_irqrestore(&proc->fileTable.lock, rflags);
    return -EBADF;
  }

  FileHandle *fh = proc->fileTable.handles[Fd];
  if (fh->isDir) {
    spin_unlock_irqrestore(&proc->fileTable.lock, rflags);
    return -EISDIR;
  }

  if (fh->offset >= fh->size) {
    spin_unlock_irqrestore(&proc->fileTable.lock, rflags);
    return 0;
  }

  size_t available = (size_t)(fh->size - fh->offset);
  size_t toRead = (Count < available) ? Count : available;
  size_t currentFileOffset = (size_t)fh->offset;

  fh->offset += toRead;
  spin_unlock_irqrestore(&proc->fileTable.lock, rflags);

  void *chunk = kmalloc(FILE_IO_CHUNK);
  if (!chunk) {
    return -ENOMEM;
  }

  size_t totalRead = 0;
  uint8_t *userDest = (uint8_t *)Buf;

  while (totalRead < toRead) {
    size_t step = toRead - totalRead;
    if (step > FILE_IO_CHUNK) {
      step = FILE_IO_CHUNK;
    }

    size_t stepRead = 0;
    if (fs_read(fh->path, currentFileOffset + totalRead, step, chunk, &stepRead) != 0 || stepRead == 0) {
      kfree(chunk);
      return totalRead > 0 ? (int64_t)totalRead : -EIO;
    }

    if (copy_to_user(userDest + totalRead, chunk, stepRead) != 0) {
      kfree(chunk);
      return -EFAULT;
    }

    totalRead += stepRead;
    if (stepRead < step) {
      break;
    }
  }

  kfree(chunk);
  return (int64_t)totalRead;
}
