#include <novaos.h>
#include <stddef.h>
#include <stdint.h>
#include <nag.h>

static const char g_vs_text[] =
"VERT\n"
"DCL IN[0]\n"
"DCL IN[1]\n"
"DCL OUT[0], POSITION\n"
"DCL OUT[1], COLOR\n"
"MOV OUT[0], IN[0]\n"
"MOV OUT[1], IN[1]\n"
"END\n";

static const char g_fs_text[] =
"FRAG\n"
"DCL IN[0], COLOR, PERSPECTIVE\n"
"DCL OUT[0], COLOR\n"
"MOV OUT[0], IN[0]\n"
"END\n";

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

int memcmp(const void *s1, const void *s2, size_t n) {
  const unsigned char *p1 = (const unsigned char *)s1;
  const unsigned char *p2 = (const unsigned char *)s2;

  while (n--) {
    if (*p1 != *p2) {
      return *p1 - *p2;
    }
    p1++;
    p2++;
  }

  return 0;
}

void *memset(void *s, int c, size_t n) {
  unsigned char *p = (unsigned char *)s;

  while (n--) {
    *p++ = (unsigned char)c;
  }

  return s;
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

            struct Vertex {
              float x, y, z, w;
              float r, g, b, a;
            };

            static const struct Vertex triangle[3] = {
              { -0.5f, -0.5f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f, 1.0f },
              {  0.5f, -0.5f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f, 1.0f },
              {  0.0f,  0.5f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 1.0f },
            };

            NagResourceCreateArgs vbArgs = {0};
            vbArgs.contextId = ctxCreate.contextId;
            vbArgs.type = NAG_RES_TYPE_BUFFER;
            vbArgs.usage = NAG_RES_USAGE_VERTEX_BUFFER;
            vbArgs.format = NAG_FORMAT_NONE;
            vbArgs.width = sizeof(triangle);
            vbArgs.height = 1;
            vbArgs.depth = 1;

            if (nagDispatch(0, NAG_GPU_OP_RESOURCE_CREATE, &vbArgs, sizeof(vbArgs)) != 0) {
              print("[VIRTIO-GPU] Failed to allocate vertex buffer\n");
              return -1;
            }

            struct Vertex *vbMapped = (struct Vertex *)vbArgs.cpuAddress;
            for (int i = 0; i < 3; i++) {
              vbMapped[i] = triangle[i];
            }

            NagTransferArgs uploadArgs = {0};
            uploadArgs.contextId = ctxCreate.contextId;
            uploadArgs.resourceId = vbArgs.resourceId;
            uploadArgs.direction = NAG_TRANSFER_TO_HOST;
            uploadArgs.x = 0;
            uploadArgs.y = 0;
            uploadArgs.z = 0;
            uploadArgs.width = sizeof(triangle);
            uploadArgs.height = 1;
            uploadArgs.depth = 1;
            uploadArgs.offset = 0;

            nagDispatch(0, NAG_GPU_OP_TRANSFER, &uploadArgs, sizeof(uploadArgs));

            NagWaitFenceArgs waitFence = {
              .fenceId = uploadArgs.fenceId,
              .timeoutMs = 1000
            };
            nagDispatch(0, NAG_GPU_OP_WAIT_FENCE, &waitFence, sizeof(waitFence));

            memset(vbMapped, 0, sizeof(triangle));

            NagTransferArgs downloadArgs = {0};
            downloadArgs.contextId = ctxCreate.contextId;
            downloadArgs.resourceId = vbArgs.resourceId;
            downloadArgs.direction = NAG_TRANSFER_FROM_HOST;
            downloadArgs.x = 0;
            downloadArgs.y = 0;
            downloadArgs.z = 0;
            downloadArgs.width = sizeof(triangle);
            downloadArgs.height = 1;
            downloadArgs.depth = 1;
            downloadArgs.offset = 0;

            nagDispatch(0, NAG_GPU_OP_TRANSFER, &downloadArgs, sizeof(downloadArgs));

            waitFence.fenceId = downloadArgs.fenceId;
            nagDispatch(0, NAG_GPU_OP_WAIT_FENCE, &waitFence, sizeof(waitFence));

            int match = (memcmp(vbMapped, triangle, sizeof(triangle)) == 0);
            print_uint(match);
            print("\n");

            // VERY evil
            uint32_t drawCmd[256];
            size_t cmdLen = 0;

            // Set Framebuffer State
            drawCmd[cmdLen++] = (5) | (0 << 8) | (3 << 16);
            drawCmd[cmdLen++] = 1; // nr_cbufs = 1
            drawCmd[cmdLen++] = 0; // zsurf_handle = 0
            drawCmd[cmdLen++] = 1; // cbuf_handle = 1

            // Create Blend State
            drawCmd[cmdLen++] = (1) | (1 << 8) | (11 << 16);
            drawCmd[cmdLen++] = 2; // handle
            drawCmd[cmdLen++] = 0; // S0
            drawCmd[cmdLen++] = 0; // S1
            drawCmd[cmdLen++] = 0x78000000; // S2[0] = RGBA mask
            drawCmd[cmdLen++] = 0; // S2[1..7]
            drawCmd[cmdLen++] = 0;
            drawCmd[cmdLen++] = 0;
            drawCmd[cmdLen++] = 0;
            drawCmd[cmdLen++] = 0;
            drawCmd[cmdLen++] = 0;
            drawCmd[cmdLen++] = 0;

            // Bind Blend State
            drawCmd[cmdLen++] = (2) | (1 << 8) | (1 << 16);
            drawCmd[cmdLen++] = 2;

            // Create DSA State
            drawCmd[cmdLen++] = (1) | (3 << 8) | (5 << 16);
            drawCmd[cmdLen++] = 3; // handle
            drawCmd[cmdLen++] = 0;
            drawCmd[cmdLen++] = 0;
            drawCmd[cmdLen++] = 0;
            drawCmd[cmdLen++] = 0;

            // Bind DSA State
            drawCmd[cmdLen++] = (2) | (3 << 8) | (1 << 16);
            drawCmd[cmdLen++] = 3;

            // Create Rasterizer State
            drawCmd[cmdLen++] = (1) | (2 << 8) | (9 << 16);
            drawCmd[cmdLen++] = 4; // handle
            drawCmd[cmdLen++] = (1 << 1) | (1 << 29); // depth_clip | half_pixel_center
            drawCmd[cmdLen++] = 0x3F800000; // point_size = 1.0f
            drawCmd[cmdLen++] = 0;
            drawCmd[cmdLen++] = 0;
            drawCmd[cmdLen++] = 0x3F800000; // line_width = 1.0f
            drawCmd[cmdLen++] = 0;
            drawCmd[cmdLen++] = 0;
            drawCmd[cmdLen++] = 0;

            // Bind Rasterizer State
            drawCmd[cmdLen++] = (2) | (2 << 8) | (1 << 16);
            drawCmd[cmdLen++] = 4;

            // Set Viewport State
            drawCmd[cmdLen++] = (4) | (0 << 8) | (7 << 16);
            drawCmd[cmdLen++] = 0; // start_slot
            drawCmd[cmdLen++] = 0x42000000; // scale_x = 32.0f
            drawCmd[cmdLen++] = 0x42000000; // scale_y = 32.0f
            drawCmd[cmdLen++] = 0x3F000000; // scale_z = 0.5f
            drawCmd[cmdLen++] = 0x42000000; // trans_x = 32.0f
            drawCmd[cmdLen++] = 0x42000000; // trans_y = 32.0f
            drawCmd[cmdLen++] = 0x3F000000; // trans_z = 0.5f

            // Create Vertex Elements
            drawCmd[cmdLen++] = (1) | (5 << 8) | (9 << 16);
            drawCmd[cmdLen++] = 5; // handle
            drawCmd[cmdLen++] = 0; // Elem 0: offset = 0
            drawCmd[cmdLen++] = 0; // divisor = 0
            drawCmd[cmdLen++] = 0; // vbuf_index = 0
            drawCmd[cmdLen++] = 31; // format = PIPE_FORMAT_R32G32B32A32_FLOAT
            drawCmd[cmdLen++] = 16; // Elem 1: offset = 16
            drawCmd[cmdLen++] = 0; // divisor = 0
            drawCmd[cmdLen++] = 0; // vbuf_index = 0
            drawCmd[cmdLen++] = 31; // format = PIPE_FORMAT_R32G32B32A32_FLOAT

            // Bind Vertex Elements
            drawCmd[cmdLen++] = (2) | (5 << 8) | (1 << 16);
            drawCmd[cmdLen++] = 5;

            // Set Vertex Buffers
            drawCmd[cmdLen++] = (6) | (0 << 8) | (3 << 16);
            drawCmd[cmdLen++] = 32; // stride = sizeof(struct Vertex)
            drawCmd[cmdLen++] = 0; // offset = 0
            drawCmd[cmdLen++] = vbArgs.resourceId; // resource handle

            // Create Vertex Shader
            size_t vs_len = sizeof(g_vs_text);
            size_t vs_dwords = (vs_len + 3) / 4;
            drawCmd[cmdLen++] = (1) | (4 << 8) | ((uint32_t)(5 + vs_dwords) << 16);
            drawCmd[cmdLen++] = 6; // handle
            drawCmd[cmdLen++] = 0; // PIPE_SHADER_VERTEX = 0
            drawCmd[cmdLen++] = (uint32_t)vs_len; // offlen in bytes (including '\0')
            drawCmd[cmdLen++] = 30; // num_tokens
            drawCmd[cmdLen++] = 0; // so_num_outputs

            uint8_t *vs_dst = (uint8_t *)&drawCmd[cmdLen];
            memset(vs_dst, 0, vs_dwords * 4);
            for (size_t i = 0; i < vs_len; i++) {
              vs_dst[i] = (uint8_t)g_vs_text[i];
            }
            cmdLen += vs_dwords;

            // Bind Vertex Shader
            drawCmd[cmdLen++] = (31) | (0 << 8) | (2 << 16);
            drawCmd[cmdLen++] = 6;
            drawCmd[cmdLen++] = 0; // PIPE_SHADER_VERTEX = 0

            // Create Fragment Shader
            size_t fs_len = sizeof(g_fs_text);
            size_t fs_dwords = (fs_len + 3) / 4;
            drawCmd[cmdLen++] = (1) | (4 << 8) | ((uint32_t)(5 + fs_dwords) << 16);
            drawCmd[cmdLen++] = 7; // handle
            drawCmd[cmdLen++] = 1; // PIPE_SHADER_FRAGMENT = 1
            drawCmd[cmdLen++] = (uint32_t)fs_len; // offlen in bytes (including '\0')
            drawCmd[cmdLen++] = 30; // num_tokens
            drawCmd[cmdLen++] = 0; // so_num_outputs

            uint8_t *fs_dst = (uint8_t *)&drawCmd[cmdLen];
            memset(fs_dst, 0, fs_dwords * 4);
            for (size_t i = 0; i < fs_len; i++) {
              fs_dst[i] = (uint8_t)g_fs_text[i];
            }
            cmdLen += fs_dwords;

            // Bind Fragment Shader
            drawCmd[cmdLen++] = (31) | (0 << 8) | (2 << 16);
            drawCmd[cmdLen++] = 7;
            drawCmd[cmdLen++] = 1; // PIPE_SHADER_FRAGMENT = 1

            // Draw VBO
            drawCmd[cmdLen++] = (8) | (0 << 8) | (12 << 16);
            drawCmd[cmdLen++] = 0; // start = 0
            drawCmd[cmdLen++] = 3; // count = 3
            drawCmd[cmdLen++] = 4; // mode = PIPE_PRIM_TRIANGLES
            drawCmd[cmdLen++] = 0; // indexed = 0
            drawCmd[cmdLen++] = 1; // instance_count = 1
            drawCmd[cmdLen++] = 0; // index_bias = 0
            drawCmd[cmdLen++] = 0; // start_instance = 0
            drawCmd[cmdLen++] = 0; // primitive_restart = 0
            drawCmd[cmdLen++] = 0; // restart_index = 0
            drawCmd[cmdLen++] = 0; // min_index = 0
            drawCmd[cmdLen++] = 2; // max_index = 2
            drawCmd[cmdLen++] = 0; // count_from_so = 0

            NagSubmitArgs drawSubmit;
            drawSubmit.contextId = ctxCreate.contextId;
            drawSubmit.commands = drawCmd;
            drawSubmit.commandSize = cmdLen * sizeof(uint32_t);
            drawSubmit.fenceId = 0;

            int64_t drawRet = nagDispatch(0, NAG_GPU_OP_SUBMIT, &drawSubmit, sizeof(drawSubmit));
            if (drawRet == 0) {
              NagWaitFenceArgs drawFence = {
                .fenceId = drawSubmit.fenceId,
                .timeoutMs = 2000
              };
              nagDispatch(0, NAG_GPU_OP_WAIT_FENCE, &drawFence, sizeof(drawFence));

              NagTransferArgs drawXfer;
              for (size_t i = 0; i < sizeof(drawXfer); i++) {
                ((uint8_t *)&drawXfer)[i] = 0;
              }
              drawXfer.contextId = ctxCreate.contextId;
              drawXfer.resourceId = resCreate.resourceId;
              drawXfer.direction = NAG_TRANSFER_FROM_HOST;
              drawXfer.width = 64;
              drawXfer.height = 64;
              drawXfer.depth = 1;

              nagDispatch(0, NAG_GPU_OP_TRANSFER, &drawXfer, sizeof(drawXfer));
              drawFence.fenceId = drawXfer.fenceId;
              nagDispatch(0, NAG_GPU_OP_WAIT_FENCE, &drawFence, sizeof(drawFence));

              volatile uint32_t *fb = (volatile uint32_t *)resCreate.cpuAddress;
              print("[VIRTIO-GPU] Pixel [0,0]: 0x");
              printHex32(fb[0]);
              print("\n");

              print("[VIRTIO-GPU] Pixel [32,32]: 0x");
              printHex32(fb[32 * 64 + 32]);
              print("\n");
            }

            NagResourceDestroyArgs vbDestroy;
            for (size_t i = 0; i < sizeof(vbDestroy); i++) {
              ((uint8_t *)&vbDestroy)[i] = 0;
            }
            vbDestroy.contextId = ctxCreate.contextId;
            vbDestroy.resourceId = vbArgs.resourceId;
            nagDispatch(0, NAG_GPU_OP_RESOURCE_DESTROY, &vbDestroy, sizeof(vbDestroy));

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
