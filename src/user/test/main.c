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

  return 0;
}
