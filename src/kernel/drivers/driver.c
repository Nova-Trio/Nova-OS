#include "driver.h"
#include <heap.h>
#include <string.h>
#include <sched.h>
#include <console.h>

static Spinlock gDriverRegistryLock = SPINLOCK_INIT;
static DriverEntry *gDriverList = NULL;

void driverInit(void) {
  gDriverList = NULL;
}

static int matchDriverName(const char *registered, const char *query) {
  if (strcmp(registered, query) == 0) {
    return 1;
  }

  const char *qBase = query;
  const char *p = query;
  while (*p) {
    if (*p == '/' || *p == '\\') {
      qBase = p + 1;
    }
    p++;
  }

  size_t regLen = strlen(registered);
  if (strncmp(registered, qBase, regLen) == 0) {
    if (qBase[regLen] == '\0' || strcmp(qBase + regLen, ".elf") == 0) {
      return 1;
    }
  }

  return 0;
}

int driverRegister(const char *name, const DriverOps *ops, void *driverPriv) {
  if (!name || !ops || !ops->ioctl) {
    return -1;
  }

  DriverEntry *entry = (DriverEntry *)kzalloc(sizeof(DriverEntry));
  if (!entry) {
    return -1;
  }

  size_t i = 0;
  while (name[i] && i + 1 < sizeof(entry->name)) {
    entry->name[i] = name[i];
    i++;
  }
  entry->name[i] = '\0';

  entry->ops = *ops;
  entry->driverPriv = driverPriv;

  uint64_t rflags = spin_lock_irqsave(&gDriverRegistryLock);
  entry->next = gDriverList;
  gDriverList = entry;
  spin_unlock_irqrestore(&gDriverRegistryLock, rflags);

  kprintf("[DRIVER] Registered driver '%s'\n", entry->name);
  return 0;
}

void driverUnregister(const char *name) {
  if (!name) return;

  uint64_t rflags = spin_lock_irqsave(&gDriverRegistryLock);
  DriverEntry **curr = &gDriverList;
  while (*curr) {
    if (strcmp((*curr)->name, name) == 0) {
      DriverEntry *target = *curr;
      *curr = target->next;
      spin_unlock_irqrestore(&gDriverRegistryLock, rflags);
      kprintf("[DRIVER] Unregistered driver '%s'\n", name);
      kfree(target);
      return;
    }
    curr = &(*curr)->next;
  }
  spin_unlock_irqrestore(&gDriverRegistryLock, rflags);
}

int driverOpen(struct Process *proc, const char *name) {
  if (!proc || !name) {
    return -1;
  }

  uint64_t rflags = spin_lock_irqsave(&gDriverRegistryLock);
  DriverEntry *driver = gDriverList;
  while (driver) {
    if (matchDriverName(driver->name, name)) {
      break;
    }
    driver = driver->next;
  }
  spin_unlock_irqrestore(&gDriverRegistryLock, rflags);

  if (!driver) {
    return -2;
  }

  void *sessionPriv = NULL;
  if (driver->ops.open) {
    int ret = driver->ops.open(driver->driverPriv, &sessionPriv);
    if (ret != 0) {
      return ret;
    }
  }

  uint64_t hFlags = spin_lock_irqsave(&proc->handleTable.lock);

  int targetSlot = -1;
  for (size_t idx = 0; idx < proc->handleTable.capacity; idx++) {
    if (!proc->handleTable.handles[idx].active) {
      targetSlot = (int)idx;
      break;
    }
  }

  if (targetSlot == -1) {
    size_t newCap = (proc->handleTable.capacity == 0) ? 8 : (proc->handleTable.capacity * 2);
    DriverHandle *newTable = (DriverHandle *)krealloc(proc->handleTable.handles, newCap * sizeof(DriverHandle));
    if (!newTable) {
      spin_unlock_irqrestore(&proc->handleTable.lock, hFlags);
      if (driver->ops.close) {
        driver->ops.close(sessionPriv);
      }
      return -12;
    }

    for (size_t idx = proc->handleTable.capacity; idx < newCap; idx++) {
      newTable[idx].driver = NULL;
      newTable[idx].sessionPriv = NULL;
      newTable[idx].active = 0;
    }

    targetSlot = (int)proc->handleTable.capacity;
    proc->handleTable.handles = newTable;
    proc->handleTable.capacity = newCap;
  }

  proc->handleTable.handles[targetSlot].driver = driver;
  proc->handleTable.handles[targetSlot].sessionPriv = sessionPriv;
  proc->handleTable.handles[targetSlot].active = 1;
  proc->handleTable.count++;

  spin_unlock_irqrestore(&proc->handleTable.lock, hFlags);
  return targetSlot;
}

int64_t driverIoctl(struct Process *proc, int handle, uint32_t cmd, void *arg, size_t argSize) {
  if (!proc || handle < 0) {
    return -9;
  }

  uint64_t hFlags = spin_lock_irqsave(&proc->handleTable.lock);
  if ((size_t)handle >= proc->handleTable.capacity || !proc->handleTable.handles[handle].active) {
    spin_unlock_irqrestore(&proc->handleTable.lock, hFlags);
    return -9;
  }

  DriverEntry *driver = proc->handleTable.handles[handle].driver;
  void *sessionPriv = proc->handleTable.handles[handle].sessionPriv;
  spin_unlock_irqrestore(&proc->handleTable.lock, hFlags);

  if (!driver || !driver->ops.ioctl) {
    return -38;
  }

  return driver->ops.ioctl(sessionPriv, cmd, arg, argSize);
}

int driverClose(struct Process *proc, int handle) {
  if (!proc || handle < 0) {
    return -9;
  }

  uint64_t hFlags = spin_lock_irqsave(&proc->handleTable.lock);
  if ((size_t)handle >= proc->handleTable.capacity || !proc->handleTable.handles[handle].active) {
    spin_unlock_irqrestore(&proc->handleTable.lock, hFlags);
    return -9;
  }

  DriverEntry *driver = proc->handleTable.handles[handle].driver;
  void *sessionPriv = proc->handleTable.handles[handle].sessionPriv;

  proc->handleTable.handles[handle].driver = NULL;
  proc->handleTable.handles[handle].sessionPriv = NULL;
  proc->handleTable.handles[handle].active = 0;
  proc->handleTable.count--;

  spin_unlock_irqrestore(&proc->handleTable.lock, hFlags);

  if (driver && driver->ops.close) {
    driver->ops.close(sessionPriv);
  }

  return 0;
}

void driverCleanupProcess(struct Process *proc) {
  if (!proc) return;

  uint64_t hFlags = spin_lock_irqsave(&proc->handleTable.lock);
  if (!proc->handleTable.handles) {
    spin_unlock_irqrestore(&proc->handleTable.lock, hFlags);
    return;
  }

  for (size_t idx = 0; idx < proc->handleTable.capacity; idx++) {
    if (proc->handleTable.handles[idx].active) {
      DriverEntry *driver = proc->handleTable.handles[idx].driver;
      void *sessionPriv = proc->handleTable.handles[idx].sessionPriv;

      proc->handleTable.handles[idx].active = 0;
      if (driver && driver->ops.close) {
        driver->ops.close(sessionPriv);
      }
    }
  }

  kfree(proc->handleTable.handles);
  proc->handleTable.handles = NULL;
  proc->handleTable.capacity = 0;
  proc->handleTable.count = 0;

  spin_unlock_irqrestore(&proc->handleTable.lock, hFlags);
}
