CC = clang
EFI_TARGET = -target x86_64-unknown-windows
SRC_DIR ?= src
BUILD_DIR ?= build

DRIVER_API_INC := -Isrc/drivers

BOOT_INCDIRS := $(shell find $(SRC_DIR)/boot -type d 2>/dev/null)
BOOT_INC_FLAGS := -I$(SRC_DIR) $(addprefix -I, $(BOOT_INCDIRS))

KERNEL_INCDIRS := $(shell find $(SRC_DIR)/kernel -type d 2>/dev/null)
KERNEL_INC_FLAGS := -I$(SRC_DIR) $(addprefix -I, $(KERNEL_INCDIRS)) $(BOOT_INC_FLAGS)

AS = nasm
ASFLAGS = -f elf64

EFI_CFLAGS = $(EFI_TARGET) -ffreestanding -fno-stack-protector -fshort-wchar -mno-red-zone -Wall -Wextra -std=c17 -MMD -MP $(BOOT_INC_FLAGS)
EFI_LDFLAGS = $(EFI_TARGET) -fuse-ld=lld -nostdlib -Wl,-entry:efi_main -Wl,-subsystem:efi_application

BOOT_SRCS := $(wildcard $(SRC_DIR)/boot/uefi/*.c)
BOOT_OBJS := $(patsubst $(SRC_DIR)/boot/uefi/%.c, $(BUILD_DIR)/boot/uefi/%.o, $(BOOT_SRCS))

KERNEL_TARGET = -target x86_64-unknown-none-elf
KERNEL_BASE_CFLAGS = $(KERNEL_TARGET) -ffreestanding -fno-stack-protector -fno-pie -fno-pic -mno-red-zone -fno-builtin -mno-sse -fno-omit-frame-pointer -g -mcmodel=kernel -Wall -Wextra -std=c17 -MMD -MP
KERNEL_CFLAGS = $(KERNEL_BASE_CFLAGS) $(KERNEL_INC_FLAGS)
DRIVER_CFLAGS = $(KERNEL_BASE_CFLAGS) $(DRIVER_API_INC)
KERNEL_LDFLAGS = $(KERNEL_TARGET) -fuse-ld=lld -nostdlib -static -Wl,-T,src/kernel/linker.ld -Wl,-z,max-page-size=0x1000
KERNEL = kernel.elf

KERNEL_C_SRCS := $(shell find $(SRC_DIR)/kernel -name '*.c')
KERNEL_ASM_SRCS := $(shell find $(SRC_DIR)/kernel -name '*.asm')

KERNEL_C_OBJS := $(patsubst $(SRC_DIR)/kernel/%.c, $(BUILD_DIR)/kernel/%.c.o, $(KERNEL_C_SRCS))
KERNEL_ASM_OBJS := $(patsubst $(SRC_DIR)/kernel/%.asm, $(BUILD_DIR)/kernel/%.asm.o, $(KERNEL_ASM_SRCS))

KERNEL_OBJS := $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS)

DRIVER_DIRS := $(shell find $(SRC_DIR)/drivers -mindepth 1 -maxdepth 1 -type d 2>/dev/null)
DRIVER_NAMES := $(notdir $(DRIVER_DIRS))
DRIVER_ELFS := $(patsubst %, $(BUILD_DIR)/drivers/%.elf, $(DRIVER_NAMES))


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


define DRIVER_RULE
$$(BUILD_DIR)/drivers/$(1)/%.c.o: $$(SRC_DIR)/drivers/$(1)/%.c
	@mkdir -p $$(dir $$@)
	$$(CC) $$(DRIVER_CFLAGS) $$(addprefix -I, $$(shell find $$(SRC_DIR)/drivers/$(1) -type d 2>/dev/null)) -c $$< -o $$@

$$(BUILD_DIR)/drivers/$(1).elf: $$(patsubst $$(SRC_DIR)/drivers/$(1)/%.c, $$(BUILD_DIR)/drivers/$(1)/%.c.o, $$(shell find $$(SRC_DIR)/drivers/$(1) -name '*.c'))
	@mkdir -p $$(dir $$@)
	ld.lld -r -o $$@ $$^
endef

$(foreach drv,$(DRIVER_NAMES),$(eval $(call DRIVER_RULE,$(drv))))

$(IMG): $(EFI) $(KERNEL) $(DRIVER_ELFS)
	dd if=/dev/zero of=$@ bs=1M count=128 status=none
ifneq ($(HAVE_PARTED),)
	parted -s $@ mklabel gpt mkpart ESP fat32 2048s 100% set 1 esp on
	mformat -i $@@@1M -F ::
	mmd -i $@@@1M ::/EFI ::/EFI/BOOT ::/EFI/novaos ::/nova ::/nova/drivers ::/nova/fw
	mcopy -i $@@@1M $(EFI) ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $@@@1M $(KERNEL) ::/EFI/novaos/$(KERNEL)
	mcopy -i $@@@1M zap-light16.psf ::/EFI/novaos/zap-light16.psf
	mcopy -i $@@@1M reallylongfilenamecros.txt ::/EFI/novaos/reallylongfilenamecros.txt
#	mcopy -i $@@@1M firmware/nvidia/booter_load.bin ::/nova/fw/booter_load.bin
#	mcopy -i $@@@1M firmware/nvidia/bootloader.bin ::/nova/fw/bootloader.bin
#	mcopy -i $@@@1M firmware/nvidia/gsp.bin ::/nova/fw/gsp.bin
	@for drv in $(DRIVER_ELFS); do \
		if [ -f "$$drv" ]; then \
			mcopy -i $@@@1M "$$drv" ::/nova/drivers/$$(basename "$$drv"); \
		fi \
	done
else
	mformat -i $@ -F ::
	mmd -i $@ ::/EFI ::/EFI/BOOT ::/EFI/novaos ::/nova ::/nova/drivers ::/nova/fw
	mcopy -i $@ $(EFI) ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $@ $(KERNEL) ::/EFI/novaos/$(KERNEL)
	mcopy -i $@ zap-light16.psf ::/EFI/novaos/zap-light16.psf
	mcopy -i $@ reallylongfilenamecros.txt ::/EFI/novaos/reallylongfilenamecros.txt
#	mcopy -i $@ firmware/nvidia/booter_load.bin ::/nova/fw/booter_load.bin
#	mcopy -i $@ firmware/nvidia/bootloader.bin ::/nova/fw/bootloader.bin
#	mcopy -i $@ firmware/nvidia/gsp.bin ::/nova/fw/gsp.bin
	@for drv in $(DRIVER_ELFS); do \
		if [ -f "$$drv" ]; then \
			mcopy -i $@ "$$drv" ::/nova/drivers/$$(basename "$$drv"); \
		fi \
	done
endif

run: $(IMG)
	qemu-system-x86_64 -bios $(OVMF) -drive file=$(IMG),format=raw,if=none,id=nvm0 -device nvme,serial=1234ffff,drive=nvm0 $(QEMU_CPU) $(QEMU_ACCEL) -M q35
	reset

run-debug: $(IMG)
	qemu-system-x86_64 -bios $(OVMF) -drive file=$(IMG),format=raw,if=none,id=nvm0 -device nvme,serial=1234ffff,drive=nvm0 $(QEMU_CPU) $(QEMU_ACCEL) -s -S -serial stdio
	reset

run-virtio: $(IMG)
	qemu-system-x86_64 -m 1G -bios $(OVMF) -drive file=$(IMG),format=raw,if=none,id=nvm0 -device nvme,serial=1234ffff,drive=nvm0 -object memory-backend-memfd,id=mem1,size=1G,share=on \
	-vga none -device virtio-vga-gl,hostmem=1G,blob=true,venus=true $(QEMU_CPU) $(QEMU_ACCEL) -M q35 -display sdl,gl=on

run-vfio: $(IMG)
	sudo qemu-system-x86_64 -m 1G -bios $(OVMF) -drive file=$(IMG),format=raw,if=none,id=nvm0 -device nvme,serial=1234ffff,drive=nvm0 $(QEMU_CPU) $(QEMU_ACCEL) -M q35 \
	-device pcie-root-port,id=root_port1,chassis=1,slot=1,bus=pcie.0 -device vfio-pci,host=01:00.0,bus=root_port1,multifunction=on,romfile=./gpu.rom -serial stdio
	reset
clean:
	rm -rf $(BUILD_DIR) $(EFI) $(KERNEL) $(IMG)

install: $(EFI) $(KERNEL) $(DRIVER_ELFS)
	sudo mkdir -p $(INSTALL_DIR)
	sudo cp $(EFI) $(INSTALL_DIR)/$(EFI)
	sudo cp $(KERNEL) $(INSTALL_DIR)/$(KERNEL)
	sudo cp zap-light16.psf $(INSTALL_DIR)/zap-light16.psf
	sudo mkdir -p $(ESP_PATH)/nova/drivers
	@for drv in $(DRIVER_ELFS); do \
		if [ -f "$$drv" ]; then \
			sudo cp "$$drv" $(ESP_PATH)/nova/drivers/$$(basename "$$drv"); \
		fi \
	done
run-hw: install
	sudo efibootmgr -n $(GRUB_BOOTNUM)
	sudo grub-reboot "nova_os"
	systemctl reboot

.PHONY: all run clean install run-hw
