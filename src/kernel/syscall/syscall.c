#include <stdint.h>
#include <stddef.h>
#include <gdt.h>
#include <pmm.h>
#include <vmm.h>
#include <console.h>

#define MSR_EFER 0xC0000080u
#define MSR_STAR 0xC0000081u
#define MSR_LSTAR 0xC0000082u
#define MSR_FMASK 0xC0000084u
#define EFER_SCE (1u << 0)

#define SYSCALL_HELLO 0

#define USER_CODE_ADDR 0x400000ULL
#define USER_STACK_ADDR 0x600000ULL

typedef struct {
  uint64_t r15, r14, r13, r12, r10, r9, r8, rdi, rsi, rdx, rcx, rbx, rax;
} SyscallFrame;

static uint8_t g_kernel_stack[16384] __attribute__((aligned(16)));

static inline uint64_t rdmsr(uint32_t msr) {
  uint32_t lo, hi;
  __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
  __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)) : "memory");
}

static void tss_load_rsp0(void) {
  Gdtr gdtr;
  uint16_t tr;
  __asm__ volatile("sgdt %0" : "=m"(gdtr));
  __asm__ volatile("str %0" : "=r"(tr));
  const uint8_t *desc = (const uint8_t *)(gdtr.base + tr);
  uint64_t qword = *(const uint64_t *)desc;
  uint64_t base = ((qword >> 56) << 24) | ((qword >> 16) & 0xFFFFFF);
  base |= (uint64_t)(*(const uint32_t *)(desc + 8)) << 32;
  ((Tss *)base)->rsp[0] = (uint64_t)g_kernel_stack + sizeof(g_kernel_stack);
}

__attribute__((used)) static long syscall_dispatch(SyscallFrame *frame) {
  switch (frame->rax) {
  case SYSCALL_HELLO:
    kprintf("hello world\n");
    return 0;
  default:
    return -1;
  }
}

// Someone move those to some other file
__attribute__((naked)) void syscall_entry(void) {
  __asm__ volatile(
    "pushq %%rcx\n\t"
    "pushq %%r11\n\t"
    "pushq %%rbp\n\t"
    "movq %%rsp, %%rbp\n\t"
    "pushq %%rax\n\t"
    "pushq %%rbx\n\t"
    "pushq %%rcx\n\t"
    "pushq %%rdx\n\t"
    "pushq %%rsi\n\t"
    "pushq %%rdi\n\t"
    "pushq %%r8\n\t"
    "pushq %%r9\n\t"
    "pushq %%r10\n\t"
    "pushq %%r12\n\t"
    "pushq %%r13\n\t"
    "pushq %%r14\n\t"
    "pushq %%r15\n\t"
    "leaq -104(%%rbp), %%rdi\n\t"
    "andq $-16, %%rsp\n\t"
    "call syscall_dispatch\n\t"
    "leaq -104(%%rbp), %%rsp\n\t"
    "popq %%r15\n\t"
    "popq %%r14\n\t"
    "popq %%r13\n\t"
    "popq %%r12\n\t"
    "popq %%r10\n\t"
    "popq %%r9\n\t"
    "popq %%r8\n\t"
    "popq %%rdi\n\t"
    "popq %%rsi\n\t"
    "popq %%rdx\n\t"
    "popq %%rcx\n\t"
    "popq %%rbx\n\t"
    "addq $8, %%rsp\n\t"
    "popq %%rbp\n\t"
    "popq %%r11\n\t"
    "popq %%rcx\n\t"
    "orq $0x200, %%r11\n\t"
    "subq $40, %%rsp\n\t"
    "movq %%rcx, (%%rsp)\n\t"
    "movq $35, 8(%%rsp)\n\t"
    "movq %%r11, 16(%%rsp)\n\t"
    "leaq 40(%%rsp), %%rcx\n\t"
    "movq %%rcx, 24(%%rsp)\n\t"
    "movq $27, 32(%%rsp)\n\t"
    "iretq\n\t"
    : : : "memory");
}

void syscall_init(void) {
  wrmsr(MSR_STAR, ((uint64_t)GDT_USER_CODE_SELECTOR << 48) | ((uint64_t)GDT_KERNEL_CODE_SELECTOR << 32));
  wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
  wrmsr(MSR_FMASK, 0);
  wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);
}

void syscall_test(void) {
  tss_load_rsp0();
  PageDirectory pml4 = vmm_get_kernel_pml4();
  void *code_phys = pmm_alloc_frames(1);
  void *stack_phys = pmm_alloc_frames(1);
  vmm_map_range(pml4, USER_CODE_ADDR, (uint64_t)code_phys, PAGE_SIZE,
                VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);
  vmm_map_range(pml4, USER_STACK_ADDR, (uint64_t)stack_phys, PAGE_SIZE,
                VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);
  static const uint8_t code[] = {0x31, 0xC0, 0x0F, 0x05, 0xEB, 0xFE};
  uint8_t *dst = (uint8_t *)USER_CODE_ADDR;
  for (size_t i = 0; i < sizeof(code); i++) {
    dst[i] = code[i];
  }
  __asm__ volatile(
    "cli\n\t"
    "pushq %2\n\t"
    "pushq %1\n\t"
    "pushq $0x202\n\t"
    "pushq %3\n\t"
    "pushq %0\n\t"
    "iretq\n\t"
    :
    : "r"((uint64_t)USER_CODE_ADDR),
      "r"((uint64_t)(USER_STACK_ADDR + PAGE_SIZE)),
      "r"((uint64_t)(GDT_USER_DATA_SELECTOR | 3)),
      "r"((uint64_t)(GDT_USER_CODE_SELECTOR | 3))
    : "memory");
}
