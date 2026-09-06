#include <novamod.h>

#define TEST_IOCTL_PING_PONG 0x1001

typedef struct PingPongPacket {
  uint32_t value;
  char message[32];
} PingPongPacket;

static int testOpen(void *driverPriv, void **sessionPriv) {
  (void)driverPriv;
  (void)sessionPriv;
  kprintf("[TEST-DRV] Client opened session\n");
  return 0;
}

static int testClose(void *sessionPriv) {
  (void)sessionPriv;
  kprintf("[TEST-DRV] Client closed session\n");
  return 0;
}

static int64_t testIoctl(void *sessionPriv, uint32_t cmd, void *arg, size_t argSize) {
  (void)sessionPriv;

  if (cmd == TEST_IOCTL_PING_PONG) {
    if (!arg || argSize < sizeof(PingPongPacket)) {
      return -22;
    }

    PingPongPacket *pkt = (PingPongPacket *)arg;
    pkt->value += 1;

    static const char reply[] = "PONG";
    for (size_t i = 0; i < sizeof(reply); i++) {
      pkt->message[i] = reply[i];
    }

    return 0;
  }

  return -22;
}

static const DriverOps testOps = {
  .open = testOpen,
  .close = testClose,
  .ioctl = testIoctl
};

int driver_init(void) {
  kprintf("[TEST-DRV] Loaded 'test' driver\n");
  return driverRegister("test", &testOps, NULL);
}

void driver_exit(void) {
  driverUnregister("test");
  kprintf("[TEST-DRV] Unloaded 'test' driver\n");
}
