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
 // hello

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

  NagContextCreateArgs ctxCreate;
  for(size_t i = 0; i < sizeof(ctxCreate); i++){
    ((uint8_t*)&ctxCreate)[i] = 0;
  }

  static const char ctxName[] = "HUANG-3D";
  for (size_t i = 0; i < sizeof(ctxName); i++) {
    ctxCreate.name[i] = ctxName[i];
  }

  int64_t ctxRet = nagDispatch(0, NAG_GPU_OP_CONTEXT_CREATE, &ctxCreate, sizeof(ctxCreate));
  if (ctxRet == 0) {
    print("[VIRTIO-GPU] Context created with ID: ");
    print_uint(ctxCreate.contextId);
    print("\n");

    NagContextDestroyArgs ctxDestroy;
    ctxDestroy.contextId = ctxCreate.contextId;

    int64_t destroyRet = nagDispatch(0, NAG_GPU_OP_CONTEXT_DESTROY, &ctxDestroy, sizeof(ctxDestroy));
    if (destroyRet == 0) {
      print("[VIRTIO-GPU] Context destroyed successfully\n");
    } else {
      print("[VIRTIO-GPU] Context destruction failed\n");
    }
  } else {
    print("[VIRTIO-GPU] Context creation failed\n");
  }

  return 0;
}
