#include "syscall.h"
#include <vmm.h>
#include <sched.h>

void sysExit(int Code) {
  (void)Code;
  Thread *Curr = schedCurrent();
  if (Curr && Curr->process) {
    vma_destroy_all(Curr->process);
    vmm_switch_pml4(vmm_get_kernel_pml4());
    vmmDestroyAddressSpace(Curr->process->pml4);
  }
  schedThreadExit();
}
