#include <stdint.h>
#include <stddef.h>
#include <gdt.h>
#include <idt.h>
#include <sched.h>
#include <pmm.h>
#include <vmm.h>
#include <console.h>
#include <nag.h>
#include <driver.h>
#include "syscall.h"

#define MSR_EFER 0xC0000080u
#define MSR_STAR 0xC0000081u
#define MSR_LSTAR 0xC0000082u
#define MSR_FMASK 0xC0000084u

#define EFER_SCE (1u << 0)
#define RFLAGS_IF (1ULL << 9)
#define RFLAGS_DF (1ULL << 10)
#define RFLAGS_TF (1ULL << 8)
#define RFLAGS_IOPL (3ULL << 12)
#define RFLAGS_NT (1ULL << 14)
#define RFLAGS_AC (1ULL << 18)

#define SYSCALL_FMASK (RFLAGS_IF | RFLAGS_DF | RFLAGS_TF | RFLAGS_IOPL | RFLAGS_NT | RFLAGS_AC)
#define SYSCALL_VECTOR 0x80

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define STR(x) STR_HELPER(x)

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

// use tss_set_rsp0 instead
static void __attribute__((unused)) tss_load_rsp0(void) {
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


__attribute__((used)) int64_t syscallDispatch(Registers *Regs) {
  switch (Regs->rax) {
    case SYS_READ:
      return sysRead((int)Regs->rdi, (void *)Regs->rsi, (size_t)Regs->rdx);

    case SYS_WRITE:
      return sysWrite((int)Regs->rdi, (const void *)Regs->rsi, (size_t)Regs->rdx);

    case SYS_OPEN:
      return sysOpen((const char *)Regs->rdi, (int)Regs->rsi, (uint32_t)Regs->rdx);

    case SYS_CLOSE:
      return sysClose((int)Regs->rdi);

    case SYS_STAT:
      return sysStat((const char *)Regs->rdi, (Stat *)Regs->rsi);

    case SYS_FSTAT:
      return sysFstat((int)Regs->rdi, (Stat *)Regs->rsi);

    case SYS_LSEEK:
      return sysLseek((int)Regs->rdi, (int64_t)Regs->rsi, (int)Regs->rdx);

    case SYS_MMAP:
      return (int64_t)sysMmap((void *)Regs->rdi, (size_t)Regs->rsi, (int)Regs->rdx, (int)Regs->r10, (int)Regs->r8, (int64_t)Regs->r9);

    case SYS_MUNMAP:
      return sysMunmap((void *)Regs->rdi, (size_t)Regs->rsi);

    case SYS_YIELD:
      return sysYield();

    case SYS_EXIT:
      sysExit((int)Regs->rdi);
      return 0;

    case SYSCALL_HELLO:
      kprintf("hello world\n");
      return 0;
    
    case SYS_NAG_DISPATCH:
      return sysNagDispatch((uint32_t)Regs->rdi, (uint32_t)Regs->rsi, (void*)Regs->rdx, (size_t)Regs->r10);

    case SYS_DRIVER_OPEN:
      return sysDriverOpen((const char *)Regs->rdi);

    case SYS_DRIVER_IOCTL:
      return sysDriverIoctl((int)Regs->rdi, (uint32_t)Regs->rsi, (void *)Regs->rdx, (size_t)Regs->r10);

    case SYS_DRIVER_CLOSE:
      return sysDriverClose((int)Regs->rdi);

    default:
      kprintf("[SYSCALL] Unhandled syscall %llu from PID %u\n", Regs->rax, (schedCurrent() && schedCurrent()->process) ? schedCurrent()->process->pid : 0);
      return -ENOSYS;
  }
}

// Someone move those to some other file
// it works so no need to move it until the great rewrite event
__attribute__((naked)) void syscallEntry(void) {
  __asm__ volatile(
    "swapgs\n\t"
    "movq %%rsp, %%gs:" STR(PERCPU_OFFSET_SCRATCH_RSP) "\n\t"
    "movq %%gs:" STR(PERCPU_OFFSET_KSTACK_TOP) ", %%rsp\n\t"
    "pushq $" STR(GDT_USER_DATA_RPL3) "\n\t"
    "pushq %%gs:" STR(PERCPU_OFFSET_SCRATCH_RSP) "\n\t"
    "pushq %%r11\n\t"
    "pushq $" STR(GDT_USER_CODE_RPL3) "\n\t"
    "pushq %%rcx\n\t"
    "pushq $0\n\t"
    "pushq $" STR(SYSCALL_VECTOR) "\n\t"
    "pushq %%rax\n\t"
    "pushq %%rbx\n\t"
    "pushq %%rcx\n\t"
    "pushq %%rdx\n\t"
    "pushq %%rsi\n\t"
    "pushq %%rdi\n\t"
    "pushq %%rbp\n\t"
    "pushq %%r8\n\t"
    "pushq %%r9\n\t"
    "pushq %%r10\n\t"
    "pushq %%r11\n\t"
    "pushq %%r12\n\t"
    "pushq %%r13\n\t"
    "pushq %%r14\n\t"
    "pushq %%r15\n\t"
    "movq %%rsp, %%rdi\n\t"
    "cld\n\t"
    "sti\n\t"
    "call syscallDispatch\n\t"
    "cli\n\t"
    "popq %%r15\n\t"
    "popq %%r14\n\t"
    "popq %%r13\n\t"
    "popq %%r12\n\t"
    "popq %%r11\n\t"
    "popq %%r10\n\t"
    "popq %%r9\n\t"
    "popq %%r8\n\t"
    "popq %%rbp\n\t"
    "popq %%rdi\n\t"
    "popq %%rsi\n\t"
    "popq %%rdx\n\t"
    "popq %%rcx\n\t"
    "popq %%rbx\n\t"
    "addq $8, %%rsp\n\t"
    "addq $16, %%rsp\n\t"
    "popq %%rcx\n\t"
    "addq $8, %%rsp\n\t"
    "popq %%r11\n\t"
    "popq %%rsp\n\t"
    "swapgs\n\t"
    "sysretq\n\t"
    : : : "memory"
  );
}

void syscallInit(void) {
  uint64_t star = (((uint64_t)(GDT_USER_DATA_SELECTOR - 8) | 3ULL) << 48) | ((uint64_t)GDT_KERNEL_CODE_SELECTOR << 32);
  wrmsr(MSR_STAR, star);
  wrmsr(MSR_LSTAR, (uint64_t)syscallEntry);
  wrmsr(MSR_FMASK, SYSCALL_FMASK);
  wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);
}

/*
void syscall_test(void) {
  //tss_load_rsp0();
  tss_set_rsp0((uint64_t)g_kernel_stack + sizeof(g_kernel_stack));
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
*/
