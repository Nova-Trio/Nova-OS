#pragma once
#include <stdint.h>
#include <stddef.h>
#include "spinlock.h"
#include <sched.h>

typedef struct Mutex {
  Spinlock lock;
  volatile int locked;
  Thread *owner;
  Thread *waitersHead;
  Thread *waitersTail;
} Mutex;

#define MUTEX_INIT { .lock = SPINLOCK_INIT, .locked = 0, .owner = NULL, .waitersHead = NULL, .waitersTail = NULL }

static inline void mutex_init(Mutex *m) {
  spinlock_init(&m->lock);
  m->locked = 0;
  m->owner = NULL;
  m->waitersHead = NULL;
  m->waitersTail = NULL;
}

static inline void mutex_lock(Mutex *m) {
  uint64_t rflags = spin_lock_irqsave(&m->lock);

  if (!m->locked) {
    m->locked = 1;
    m->owner = schedCurrent();
    spin_unlock_irqrestore(&m->lock, rflags);
    return;
  }

  Thread *curr = schedCurrent();
  curr->waitNext = NULL;

  if (m->waitersTail) {
    m->waitersTail->waitNext = curr;
  } else {
    m->waitersHead = curr;
  }
  m->waitersTail = curr;

  schedBlockCurrent(&m->lock, rflags);
}

static inline void mutex_unlock(Mutex *m) {
  uint64_t rflags = spin_lock_irqsave(&m->lock);

  if (m->waitersHead) {
    Thread *wake = m->waitersHead;
    m->waitersHead = wake->waitNext;
    if (!m->waitersHead) {
      m->waitersTail = NULL;
    }
    wake->waitNext = NULL;

    m->owner = wake;
    spin_unlock_irqrestore(&m->lock, rflags);

    schedEnqueueReady(wake);
  } else {
    m->locked = 0;
    m->owner = NULL;
    spin_unlock_irqrestore(&m->lock, rflags);
  }
}

typedef struct WaitQueue {
  Spinlock lock;
  Thread *head;
  Thread *tail;
} WaitQueue;

#define WAITQUEUE_INIT { .lock = SPINLOCK_INIT, .head = NULL, .tail = NULL }

static inline void wait_queue_init(WaitQueue *wq) {
  spinlock_init(&wq->lock);
  wq->head = NULL;
  wq->tail = NULL;
}

static inline void wait_queue_wake_all(WaitQueue *wq) {
  uint64_t rflags = spin_lock_irqsave(&wq->lock);

  Thread *curr = wq->head;
  wq->head = NULL;
  wq->tail = NULL;

  spin_unlock_irqrestore(&wq->lock, rflags);

  while (curr) {
    Thread *next = curr->waitNext;
    curr->waitNext = NULL;
    schedEnqueueReady(curr);
    curr = next;
  }
}

#define wait_event_timeout(wq, condition, timeout_ms) ({                       \
uint64_t __start = hpet_get_millis();                                          \
int __ret = 1;                                                                 \
while (!(condition)) {                                                         \
  if ((hpet_get_millis() - __start) >= (uint64_t)(timeout_ms)) {               \
    __ret = 0;                                                                 \
    break;                                                                     \
  }                                                                            \
  uint64_t __flags = spin_lock_irqsave(&(wq).lock);                            \
  if (condition) {                                                             \
    spin_unlock_irqrestore(&(wq).lock, __flags);                               \
    break;                                                                     \
  }                                                                            \
  Thread *__curr = schedCurrent();                                             \
  __curr->waitNext = NULL;                                                     \
  if ((wq).tail) {                                                             \
    (wq).tail->waitNext = __curr;                                              \
  } else {                                                                     \
    (wq).head = __curr;                                                        \
  }                                                                            \
  (wq).tail = __curr;                                                          \
  schedBlockCurrent(&(wq).lock, __flags);                                      \
}                                                                              \
__ret;                                                                         \
})
