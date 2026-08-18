CC = clang
EFI_TARGET = -target x86_64-unknown-windows
SRC_DIR ?= src
INCDIRS := $(shell find $(SRC_DIR) -type d)
INC_FLAGS := $(addprefix -I, $(INCDIRS))

EFI_CFLAGS = $(EFI_TARGET) -ffreestanding -fno-stack-protector -fshort-wchar -mno-red-zone -Wall -Wextra -std=c17 -MMD -MP $(INC_FLAGS)
EFI_LDFLAGS = $(EFI_TARGET) -fuse-ld=lld -nostdlib -Wl,-entry:efi_main -Wl,-subsystem:efi_application

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
BUILD_DIR = build

ESP_PATH ?= /boot
INSTALL_DIR = $(ESP_PATH)/EFI/novaos
GRUB_BOOTNUM ?= 0001

all: $(IMG)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

-include $(BUILD_DIR)/*.d

$(BUILD_DIR)/%.o: $(SRC_DIR)/boot/%.c | $(BUILD_DIR)
	$(CC) $(EFI_CFLAGS) -c $< -o $@

$(EFI): $(BUILD_DIR)/bootloader.o
	$(CC) $(EFI_LDFLAGS) $< -o $@

$(IMG): $(EFI)
	dd if=/dev/zero of=$@ bs=1M count=64 status=none
ifneq ($(HAVE_PARTED),)
	parted -s $@ mklabel gpt mkpart ESP fat32 2048s 100% set 1 esp on
	mformat -i $@@@1M -F ::
	mmd -i $@@@1M ::/EFI ::/EFI/BOOT
	mcopy -i $@@@1M $< ::/EFI/BOOT/$<
else
	mformat -i $@ -F ::
	mmd -i $@ ::/EFI ::/EFI/BOOT
	mcopy -i $@ $< ::/EFI/BOOT/$<
endif

run: $(IMG)
	qemu-system-x86_64 -bios $(OVMF) -drive file=$(IMG),format=raw $(QEMU_CPU) $(QEMU_ACCEL) -serial stdio
	reset

run-debug: $(IMG)
	qemu-system-x86_64 -bios $(OVMF) -drive file=$(IMG),format=raw $(QEMU_CPU) $(QEMU_ACCEL) -s -S -serial stdio
	reset

clean:
	rm -rf $(BUILD_DIR) $(EFI) $(IMG)

install: $(EFI)
	sudo mkdir -p $(INSTALL_DIR)
	sudo cp $(EFI) $(INSTALL_DIR)/$(EFI)

run-hw: install
	sudo efibootmgr -n $(GRUB_BOOTNUM)
	sudo grub-reboot "nova_os"
	systemctl reboot

.PHONY: all run clean install run-hw
