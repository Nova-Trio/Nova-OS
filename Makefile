CC = clang
EFI_TARGET = -target x86_64-unknown-windows
SRC_DIR ?= src
BUILD_DIR ?= build
INCDIRS := $(shell find $(SRC_DIR) -type d)
INC_FLAGS := $(addprefix -I, $(INCDIRS))

AS = nasm
ASFLAGS = -f elf64

EFI_CFLAGS = $(EFI_TARGET) -ffreestanding -fno-stack-protector -fshort-wchar -mno-red-zone -Wall -Wextra -std=c17 -MMD -MP $(INC_FLAGS)
EFI_LDFLAGS = $(EFI_TARGET) -fuse-ld=lld -nostdlib -Wl,-entry:efi_main -Wl,-subsystem:efi_application

BOOT_SRCS := $(wildcard $(SRC_DIR)/boot/uefi/*.c)
BOOT_OBJS := $(patsubst $(SRC_DIR)/boot/uefi/%.c, $(BUILD_DIR)/boot/uefi/%.o, $(BOOT_SRCS))

KERNEL_TARGET = -target x86_64-unknown-none-elf
KERNEL_CFLAGS = $(KERNEL_TARGET) -ffreestanding -fno-stack-protector -fno-pie -fno-pic -mno-red-zone -fno-builtin -mno-sse -fno-omit-frame-pointer -g -mcmodel=kernel -Wall -Wextra -std=c17 -MMD -MP $(INC_FLAGS)
KERNEL_LDFLAGS = $(KERNEL_TARGET) -fuse-ld=lld -nostdlib -static -Wl,-T,src/kernel/linker.ld -Wl,-z,max-page-size=0x1000
KERNEL = kernel.elf

KERNEL_C_SRCS := $(shell find $(SRC_DIR)/kernel -name '*.c')
KERNEL_ASM_SRCS := $(shell find $(SRC_DIR)/kernel -name '*.asm')

KERNEL_C_OBJS := $(patsubst $(SRC_DIR)/kernel/%.c, $(BUILD_DIR)/kernel/%.c.o, $(KERNEL_C_SRCS))
KERNEL_ASM_OBJS := $(patsubst $(SRC_DIR)/kernel/%.asm, $(BUILD_DIR)/kernel/%.asm.o, $(KERNEL_ASM_SRCS))

KERNEL_OBJS := $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS)


OVMF_PATHS := /usr/share/edk2/x64/OVMF.4m.fd $(wildcard fw/*.fd)

OVMF ?= $(firstword $(wildcard $(OVMF_PATHS)))

ifeq ($(OS),Windows_NT)
    QEMU_ACCEL ?=
    QEMU_CPU ?= -cpu qemu64
else
    ifeq ($(shell uname -s),Linux)
        ifneq ($(wildcard /dev/kvm),)
            QEMU_ACCEL ?= -accel kvm
            QEMU_CPU ?= -cpu host
        else
            QEMU_ACCEL ?= -accel tcg
            QEMU_CPU ?= -cpu qemu64
        endif
    else
        QEMU_ACCEL ?=
        QEMU_CPU ?= -cpu qemu64
    endif
endif

HAVE_PARTED := $(shell command -v parted 2> /dev/null)

IMG = disk.img

EFI = BOOTX64.EFI

ESP_PATH ?= /boot
INSTALL_DIR = $(ESP_PATH)/EFI/novaos
GRUB_BOOTNUM ?= 0001

all: $(IMG)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

$(BUILD_DIR)/boot/uefi/%.o: $(SRC_DIR)/boot/uefi/%.c
	@mkdir -p $(dir $@)
	$(CC) $(EFI_CFLAGS) -c $< -o $@

$(EFI): $(BOOT_OBJS)
	$(CC) $(EFI_LDFLAGS) $^ -o $@

$(BUILD_DIR)/kernel/%.c.o: $(SRC_DIR)/kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@


$(BUILD_DIR)/kernel/%.asm.o: $(SRC_DIR)/kernel/%.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(KERNEL): $(KERNEL_OBJS)
	$(CC) $(KERNEL_LDFLAGS) $^ -o $@

$(IMG): $(EFI) $(KERNEL)
	dd if=/dev/zero of=$@ bs=1M count=64 status=none
ifneq ($(HAVE_PARTED),)
	parted -s $@ mklabel gpt mkpart ESP fat32 2048s 100% set 1 esp on
	mformat -i $@@@1M -F ::
	mmd -i $@@@1M ::/EFI ::/EFI/BOOT ::/EFI/novaos
	mcopy -i $@@@1M $(EFI) ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $@@@1M $(KERNEL) ::/EFI/novaos/$(KERNEL)
	mcopy -i $@@@1M zap-light16.psf ::/EFI/novaos/zap-light16.psf
	mcopy -i $@@@1M reallylongfilenamecros.txt ::/EFI/novaos/reallylongfilenamecros.txt
else
	mformat -i $@ -F ::
	mmd -i $@ ::/EFI ::/EFI/BOOT ::/EFI/novaos
	mcopy -i $@ $(EFI) ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $@ $(KERNEL) ::/EFI/novaos/$(KERNEL)
	mcopy -i $@ zap-light16.psf ::/EFI/novaos/zap-light16.psf
	mcopy -i $@ reallylongfilenamecros.txt ::/EFI/novaos/reallylongfilenamecros.txt
endif

run: $(IMG)
	qemu-system-x86_64 -bios $(OVMF) -drive file=$(IMG),format=raw,if=none,id=nvm0 -device nvme,serial=1234ffff,drive=nvm0 $(QEMU_CPU) $(QEMU_ACCEL) -M q35
	reset

run-debug: $(IMG)
	qemu-system-x86_64 -bios $(OVMF) -drive file=$(IMG),format=raw,if=none,id=nvm0 -device nvme,serial=1234ffff,drive=nvm0 $(QEMU_CPU) $(QEMU_ACCEL) -s -S -serial stdio
	reset


run-vfio: $(IMG)
	sudo qemu-system-x86_64 -bios $(OVMF) -drive file=$(IMG),format=raw,if=none,id=nvm0 -device nvme,serial=1234ffff,drive=nvm0 $(QEMU_CPU) $(QEMU_ACCEL) -M q35 -device pcie-root-port,id=root_port1,chassis=1,slot=1,bus=pcie.0 -device vfio-pci,host=01:00.0,bus=root_port1,multifunction=on
	reset
clean:
	rm -rf $(BUILD_DIR) $(EFI) $(KERNEL) $(IMG)

install: $(EFI) $(KERNEL)
	sudo mkdir -p $(INSTALL_DIR)
	sudo cp $(EFI) $(INSTALL_DIR)/$(EFI)
	sudo cp $(KERNEL) $(INSTALL_DIR)/$(KERNEL)
	sudo cp zap-light16.psf $(INSTALL_DIR)/zap-light16.psf
run-hw: install
	sudo efibootmgr -n $(GRUB_BOOTNUM)
	sudo grub-reboot "nova_os"
	systemctl reboot

.PHONY: all run clean install run-hw
