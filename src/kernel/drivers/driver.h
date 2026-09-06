#pragma once
#include <stdint.h>
#include <stddef.h>
#include <spinlock.h>

typedef struct DriverOps {
  int (*open)(void *driverPriv, void **sessionPriv);
  int (*close)(void *sessionPriv);
  int64_t (*ioctl)(void *sessionPriv, uint32_t cmd, void *arg, size_t argSize);
} DriverOps;

typedef struct DriverEntry {
  char name[64];
  DriverOps ops;
  void *driverPriv;
  struct DriverEntry *next;
} DriverEntry;

typedef struct DriverHandle {
  DriverEntry *driver;
  void *sessionPriv;
  int active;
} DriverHandle;

typedef struct DriverHandleTable {
  DriverHandle *handles;
  size_t capacity;
  size_t count;
  Spinlock lock;
} DriverHandleTable;

struct Process;

void driverInit(void);
int driverRegister(const char *name, const DriverOps *ops, void *driverPriv);
void driverUnregister(const char *name);

int driverOpen(struct Process *proc, const char *name);
int64_t driverIoctl(struct Process *proc, int handle, uint32_t cmd, void *arg, size_t argSize);
int driverClose(struct Process *proc, int handle);
void driverCleanupProcess(struct Process *proc);
