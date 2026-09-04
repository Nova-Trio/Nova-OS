#pragma once
#include <stdint.h>

typedef struct {
  volatile uint32_t lock;
} Spinlock;

#define SPINLOCK_INIT ((Spinlock){ .lock = 0 })

static inline void spinlock_init(Spinlock *lock) {
  lock->lock = 0;
}

static inline void spin_lock(Spinlock *lock) {
  while (__atomic_test_and_set(&lock->lock, __ATOMIC_ACQUIRE)) {
    while (__atomic_load_n(&lock->lock, __ATOMIC_RELAXED)) {
      __asm__ volatile("pause");
    }
  }
}

static inline void spin_unlock(Spinlock *lock) {
  __atomic_clear(&lock->lock, __ATOMIC_RELEASE);
}

static inline uint64_t spin_lock_irqsave(Spinlock *lock) {
  uint64_t rflags;
  __asm__ volatile("pushfq; popq %0; cli" : "=r"(rflags) : : "memory");
  spin_lock(lock);
  return rflags;
}

static inline void spin_unlock_irqrestore(Spinlock *lock, uint64_t rflags) {
  spin_unlock(lock);
  __asm__ volatile("pushq %0; popfq" : : "r"(rflags) : "memory");
}
