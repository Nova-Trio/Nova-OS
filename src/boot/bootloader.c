#include <efi.h>

#define EFI_WHITE_FG 0x0F
#define EFI_BLACK_BG 0x00


EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
  (void)ImageHandle;
  EFI_INPUT_KEY Key;
  SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
  SystemTable->ConOut->SetAttribute(SystemTable->ConOut, (EFI_WHITE_FG | EFI_BLACK_BG << 4));
  SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16 *)u"Hello, Cros!\r\n");


  SystemTable->ConIn->Reset(SystemTable->ConIn, 0);
  while (SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key) != EFI_SUCCESS) {
    __asm__ volatile("pause");
  }
  return EFI_SUCCESS;
}
