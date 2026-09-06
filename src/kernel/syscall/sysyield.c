#include "syscall.h"
#include <sched.h>

int64_t sysYield(void) {
  schedYield();
  return 0;
}
