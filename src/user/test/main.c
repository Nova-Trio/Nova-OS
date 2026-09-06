#include <novaos.h>
#include <stddef.h>
#include <stdint.h>
#include <nag.h>

static void print_uint(uint64_t val) {
  if (val == 0) {
    print("0");
    return;
  }
  char buf[32];
  int i = 0;
  while (val > 0) {
    buf[i++] = (char)('0' + (val % 10));
    val /= 10;
  }
  char out[32];
  for (int j = 0; j < i; j++) {
    out[j] = buf[i - 1 - j];
  }
  out[i] = '\0';
  print(out);
}

int main(int argc, char **argv, char **envp) {
  (void)envp;
  (void)argv;
  (void)argc;

  print("nova os no usermode phase is over :pensive:\n");

/*

  NagGpuProps props;
  for (size_t i = 0; i < sizeof(props); i++) {
    ((uint8_t *)&props)[i] = 0;
  }

  int64_t gpuRet = nagDispatch(0, NAG_GPU_OP_QUERY, &props, sizeof(props));
  if (gpuRet == 0) {
    print("[VIRTIO-GPU] Max Display: ");
    print_uint(props.maxDisplayWidth);
    print("x");
    print_uint(props.maxDisplayHeight);
    print("\n");
  } else {
    print("[VIRTIO-GPU] Query failed\n");
  }



  #define TEST_IOCTL_PING_PONG 0x1001

  typedef struct PingPongPacket {
    uint32_t value;
    char message[32];
  } PingPongPacket;

  int handle = driverOpen("test");
  if (handle < 0) {
    handle = driverOpen("/nova/drivers/test.elf");
  }

  if (handle < 0) {
    print("Error: Failed to open driver 'test'\n");
    return 1;
  }

  PingPongPacket pkt;
  pkt.value = 42;
  static const char ping[] = "PING";
  for (size_t i = 0; i < sizeof(ping); i++) {
    pkt.message[i] = ping[i];
  }

  print("Sending: value=");
  print_uint(pkt.value);
  print(", msg=\"");
  print(pkt.message);
  print("\"\n");

  int64_t ret = driverIoctl(handle, TEST_IOCTL_PING_PONG, &pkt, sizeof(pkt));
  if (ret == 0) {
    print("Received: value=");
    print_uint(pkt.value);
    print(", msg=\"");
    print(pkt.message);
    print("\"\n");
  } else {
    print("IOCTL failed\n");
  }

  driverClose(handle);
  print("Closed session successfully.\n");
*/
  return 0;
}
