#include <novamod.h>

int driver_init(void) {
  kprintf("I am loaded.\n");
  return 0;
}

void driver_exit(void) {
  kprintf("I am unloaded.\n");
}
