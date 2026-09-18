#include "syscall.h"
#include <vmm.h>
#include <sched.h>
#include <heap.h>
#include <fat32.h>
#include <string.h>

static char *copyStringFromUser(const char *userStr) {
  if (!userStr) {
    return NULL;
  }

  size_t capacity = 64;
  size_t len = 0;
  char *buf = (char *)kmalloc(capacity);
  if (!buf) {
    return NULL;
  }

  while (1) {
    char c;
    if (copy_from_user(&c, userStr + len, 1) != 0) {
      kfree(buf);
      return NULL;
    }

    if (len + 1 >= capacity) {
      size_t newCap = capacity * 2;
      char *newBuf = (char *)krealloc(buf, newCap);
      if (!newBuf) {
        kfree(buf);
        return NULL;
      }
      buf = newBuf;
      capacity = newCap;
    }

    buf[len] = c;
    if (c == '\0') {
      break;
    }
    len++;
  }

  return buf;
}

int64_t sysOpen(const char *Path, int Flags, uint32_t Mode) {
  (void)Mode;
  char *kpath = copyStringFromUser(Path);
  if (!kpath) {
    return -EFAULT;
  }

  Fat32DirEntry entry;
  if (fs_stat(kpath, &entry) != 0) {
    if (kpath[0] == '/' && fs_stat(kpath + 1, &entry) == 0) {
      size_t len = strlen(kpath + 1);
      memmove(kpath, kpath + 1, len + 1);
    } else {
      kfree(kpath);
      return -ENOENT;
    }
  }

  Thread *curr = schedCurrent();
  if (!curr || !curr->process) {
    kfree(kpath);
    return -EPERM;
  }
  Process *proc = curr->process;

  uint64_t rflags = spin_lock_irqsave(&proc->fileTable.lock);

  int fd = -1;
  for (size_t i = 3; i < proc->fileTable.capacity; i++) {
    if (!proc->fileTable.handles[i]) {
      fd = (int)i;
      break;
    }
  }

  if (fd < 0) {
    size_t oldCap = proc->fileTable.capacity;
    size_t newCap = oldCap == 0 ? 16 : oldCap * 2;
    FileHandle **newHandles = (FileHandle **)kzalloc(newCap * sizeof(FileHandle *));
    if (!newHandles) {
      spin_unlock_irqrestore(&proc->fileTable.lock, rflags);
      kfree(kpath);
      return -ENOMEM;
    }

    if (proc->fileTable.handles) {
      for (size_t i = 0; i < oldCap; i++) {
        newHandles[i] = proc->fileTable.handles[i];
      }
      kfree(proc->fileTable.handles);
    }

    proc->fileTable.handles = newHandles;
    proc->fileTable.capacity = newCap;
    fd = (oldCap < 3) ? 3 : (int)oldCap;
  }

  FileHandle *fh = (FileHandle *)kzalloc(sizeof(FileHandle));
  if (!fh) {
    spin_unlock_irqrestore(&proc->fileTable.lock, rflags);
    kfree(kpath);
    return -ENOMEM;
  }

  fh->path = kpath;
  fh->offset = 0;
  fh->size = entry.file_size;
  fh->flags = (uint32_t)Flags;
  fh->isDir = entry.is_directory;

  proc->fileTable.handles[fd] = fh;
  spin_unlock_irqrestore(&proc->fileTable.lock, rflags);

  return (int64_t)fd;
}

int64_t sysClose(int Fd) {
  if (Fd < 3) {
    return 0;
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
  proc->fileTable.handles[Fd] = NULL;
  spin_unlock_irqrestore(&proc->fileTable.lock, rflags);

  if (fh) {
    if (fh->path) {
      kfree(fh->path);
    }
    kfree(fh);
  }

  return 0;
}

int64_t sysLseek(int Fd, int64_t Offset, int Whence) {
  if (Fd < 3) {
    return -ESPIPE;
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
  int64_t newOffset = 0;

  switch (Whence) {
    case SEEK_SET:
      newOffset = Offset;
      break;
    case SEEK_CUR:
      newOffset = (int64_t)fh->offset + Offset;
      break;
    case SEEK_END:
      newOffset = (int64_t)fh->size + Offset;
      break;
    default:
      spin_unlock_irqrestore(&proc->fileTable.lock, rflags);
      return -EINVAL;
  }

  if (newOffset < 0) {
    spin_unlock_irqrestore(&proc->fileTable.lock, rflags);
    return -EINVAL;
  }

  fh->offset = (uint64_t)newOffset;
  spin_unlock_irqrestore(&proc->fileTable.lock, rflags);

  return newOffset;
}

int64_t sysStat(const char *Path, Stat *StatBuf) {
  char *kpath = copyStringFromUser(Path);
  if (!kpath) {
    return -EFAULT;
  }

  Fat32DirEntry entry;
  if (fs_stat(kpath, &entry) != 0) {
    if (kpath[0] == '/' && fs_stat(kpath + 1, &entry) == 0) {
    } else {
      kfree(kpath);
      return -ENOENT;
    }
  }
  kfree(kpath);

  Stat st;
  memset(&st, 0, sizeof(st));
  st.st_size = entry.file_size;
  st.st_mode = entry.is_directory ? (S_IFDIR | 0755) : (S_IFREG | 0755);
  st.st_blksize = 4096;
  st.st_blocks = (entry.file_size + 511) / 512;
  st.st_nlink = 1;

  if (copy_to_user(StatBuf, &st, sizeof(st)) != 0) {
    return -EFAULT;
  }

  return 0;
}

int64_t sysFstat(int Fd, Stat *StatBuf) {
  Thread *curr = schedCurrent();
  if (!curr || !curr->process) {
    return -EPERM;
  }
  Process *proc = curr->process;

  Stat st;
  memset(&st, 0, sizeof(st));

  if (Fd == 0 || Fd == 1 || Fd == 2) {
    st.st_mode = S_IFCHR | 0666;
    st.st_blksize = 1024;
    if (copy_to_user(StatBuf, &st, sizeof(st)) != 0) {
      return -EFAULT;
    }
    return 0;
  }

  uint64_t rflags = spin_lock_irqsave(&proc->fileTable.lock);
  if ((size_t)Fd >= proc->fileTable.capacity || !proc->fileTable.handles[Fd]) {
    spin_unlock_irqrestore(&proc->fileTable.lock, rflags);
    return -EBADF;
  }

  FileHandle *fh = proc->fileTable.handles[Fd];
  st.st_size = fh->size;
  st.st_mode = fh->isDir ? (S_IFDIR | 0755) : (S_IFREG | 0755);
  st.st_blksize = 4096;
  st.st_blocks = (fh->size + 511) / 512;
  st.st_nlink = 1;

  spin_unlock_irqrestore(&proc->fileTable.lock, rflags);

  if (copy_to_user(StatBuf, &st, sizeof(st)) != 0) {
    return -EFAULT;
  }

  return 0;
}

void fileTableCleanup(Process *proc) {
  if (!proc) {
    return;
  }

  uint64_t rflags = spin_lock_irqsave(&proc->fileTable.lock);
  if (proc->fileTable.handles) {
    for (size_t i = 0; i < proc->fileTable.capacity; i++) {
      FileHandle *fh = proc->fileTable.handles[i];
      if (fh) {
        if (fh->path) {
          kfree(fh->path);
        }
        kfree(fh);
      }
    }
    kfree(proc->fileTable.handles);
    proc->fileTable.handles = NULL;
    proc->fileTable.capacity = 0;
  }
  spin_unlock_irqrestore(&proc->fileTable.lock, rflags);
}
