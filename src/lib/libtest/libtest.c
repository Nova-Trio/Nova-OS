#include <stdio.h>

__attribute__((constructor)) static void testInit(void) {
  printf("[libtest] android in bios?\n");
}

int testAdd(int a, int b) {
  return a + b;
}

const char *testMessage(void) {
  return "Hello!";
}
