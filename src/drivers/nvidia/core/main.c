#include <novamod.h>
#include <nv_device.h>

int driver_init(void) {
  kprintf("[NV] Loading NVIDIA graphics driver...\n");
  int res = nv_device_probe();
  if (res != 0) {
    kprintf("[NV] No compatible NVIDIA GPU initialized\n");
    return res;
  }
  return 0;
}

void driver_exit(void) {
  kprintf("[NV] Unloading NVIDIA graphics driver...\n");
  nv_device_remove_all();
}
