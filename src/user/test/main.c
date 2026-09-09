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
 
static void printHex32(uint32_t v){
  static const char hexChars[] = "0123456789ABCDEF";
  char buf[9];
  for (int i = 7; i >= 0; i--){
    buf[i] = hexChars[v & 0xF];
    v >>= 4;
  }
  buf[8] = '\0';
  print(buf);
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

    NagResourceCreateArgs resCreate;
    for(size_t i = 0; i < sizeof(resCreate); i++){
      ((uint8_t*)&resCreate)[i] = 0;
    }

    resCreate.contextId = ctxCreate.contextId;
    resCreate.type = NAG_RES_TYPE_2D;
    resCreate.format = NAG_FORMAT_B8G8R8A8_UNORM;
    resCreate.usage = NAG_RES_USAGE_RENDER_TARGET;
    resCreate.width = 64;
    resCreate.height = 64;
    resCreate.depth = 1;

    int64_t resRet = nagDispatch(0, NAG_GPU_OP_RESOURCE_CREATE, &resCreate, sizeof(resCreate));
    if (resRet == 0) {
      print("[VIRTIO-GPU] Resource created with ID: ");
      print_uint(resCreate.resourceId);
      print(", Size: ");
      print_uint(resCreate.size);
      print(" bytes\n");

      // very evil commands
      uint32_t cmd[] = {
        // create surface object for this 3D res
        (1) | (8 << 8) | (5 << 16),
        1,
        resCreate.resourceId,
        1,
        0,
        0,

        // set fb state
        (5) | (0 << 8) | (3 << 16),
        1,
        0,
        1,

        // clear render target to r1.0f g0.0f b0.0f a1.0f
        (7) | (0 << 8) | (8 << 16),
        4,
        0x3F800000,
        0x00000000,
        0x00000000,
        0x3F800000,
        0,
        0,
        0
      };

      NagSubmitArgs submitArgs;
      submitArgs.contextId = ctxCreate.contextId;
      submitArgs.commands = cmd;
      submitArgs.commandSize = sizeof(cmd);
      submitArgs.fenceId = 0;

      int64_t submitRet = nagDispatch(0, NAG_GPU_OP_SUBMIT, &submitArgs, sizeof(submitArgs));
      if (submitRet == 0) {
        print("[VIRTIO-GPU] 3D Command stream submitted. Fence: ");
        print_uint(submitArgs.fenceId);
        print("\n");

        NagWaitFenceArgs waitArgs;
        waitArgs.fenceId = submitArgs.fenceId;
        waitArgs.timeoutMs = 2000;

        int64_t waitRet = nagDispatch(0, NAG_GPU_OP_WAIT_FENCE, &waitArgs, sizeof(waitArgs));
        if (waitRet == 0) {
          print("[VIRTIO-GPU] Fence completed.\n");

          NagTransferArgs xferArgs;
          for (size_t i = 0; i < sizeof(xferArgs); i++) {
            ((uint8_t *)&xferArgs)[i] = 0;
          }
          xferArgs.contextId = ctxCreate.contextId;
          xferArgs.resourceId = resCreate.resourceId;
          xferArgs.direction = NAG_TRANSFER_FROM_HOST;
          xferArgs.x = 0;
          xferArgs.y = 0;
          xferArgs.z = 0;
          xferArgs.width = 64;
          xferArgs.height = 64;
          xferArgs.depth = 1;
          xferArgs.offset = 0;

          int64_t xferRet = nagDispatch(0, NAG_GPU_OP_TRANSFER, &xferArgs, sizeof(xferArgs));
          if (xferRet == 0) {
            print("[VIRTIO-GPU] Host transfer completed.\n");

            volatile uint32_t *pixels = (volatile uint32_t *)resCreate.cpuAddress;
            print("[VIRTIO-GPU] Pixel [0,0] (B8G8R8A8): 0x");
            printHex32(pixels[0]);
            print("\n");
          } else {
            print("[VIRTIO-GPU] Host transfer failed.\n");
          }
        } else {
          print("[VIRTIO-GPU] Fence wait timed out.\n");
        }
      } else {
        print("[VIRTIO-GPU] Command submission failed.\n");
      }

      NagResourceDestroyArgs resDestroy;
      resDestroy.contextId = ctxCreate.contextId;
      resDestroy.resourceId = resCreate.resourceId;

      int64_t resDestroyRet = nagDispatch(0, NAG_GPU_OP_RESOURCE_DESTROY, &resDestroy, sizeof(resDestroy));
      if (resDestroyRet == 0) {
        print("[VIRTIO-GPU] Resource destroyed successfully\n");
      } else {
        print("[VIRTIO-GPU] Resource destruction failed\n");
      }
    } else {
      print("[VIRTIO-GPU] Resource creation failed\n");
    }

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
