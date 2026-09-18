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

USER_TARGET = -target x86_64-unknown-none-elf
USER_BASE_CFLAGS = $(USER_TARGET) -ffreestanding -fno-stack-protector -fno-builtin -fno-omit-frame-pointer -mno-red-zone -mcmodel=small -Wall -Wextra -std=c17 -MMD -MP

USER_SHARED_HEADERS := $(SRC_DIR)/user/novaos.h $(SRC_DIR)/user/nag.h
USER_STAGED_INC := $(BUILD_DIR)/include/user_abi

LIB_INCDIRS := $(shell find $(SRC_DIR)/lib -type d 2>/dev/null)
LIB_INC_FLAGS := -I$(SRC_DIR)/lib $(addprefix -I, $(LIB_INCDIRS)) -I$(USER_STAGED_INC)

USER_API_INC := -I$(SRC_DIR)/user -I$(SRC_DIR)/lib/libc/include
USER_INCDIRS := $(shell find $(SRC_DIR)/user -type d 2>/dev/null)
USER_INC_FLAGS := $(USER_API_INC) $(addprefix -I, $(USER_INCDIRS))

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

USER_CRT0 := $(BUILD_DIR)/user/crt0.o

$(USER_CRT0): $(SRC_DIR)/user/crt0.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(USER_STAGED_INC)/.stamp: $(USER_SHARED_HEADERS)
	@mkdir -p $(USER_STAGED_INC)/user
	@cp $(SRC_DIR)/user/novaos.h $(USER_STAGED_INC)/novaos.h
	@cp $(SRC_DIR)/user/novaos.h $(USER_STAGED_INC)/user/novaos.h
	@cp $(SRC_DIR)/user/nag.h $(USER_STAGED_INC)/nag.h
	@cp $(SRC_DIR)/user/nag.h $(USER_STAGED_INC)/user/nag.h
	@touch $@

RTLD_SO := $(BUILD_DIR)/lib/ld-crow.so
RTLD_CFLAGS := $(USER_BASE_CFLAGS) -fPIC -fvisibility=hidden $(LIB_INC_FLAGS)
RTLD_LDFLAGS := $(USER_TARGET) -fuse-ld=lld -nostdlib -shared -Bsymbolic -Wl,-e,_start -Wl,-z,max-page-size=0x1000

$(BUILD_DIR)/lib/ldso/%.c.o: $(SRC_DIR)/lib/ldso/%.c $(USER_STAGED_INC)/.stamp
	@mkdir -p $(dir $@)
	$(CC) $(RTLD_CFLAGS) -c $< -o $@

$(BUILD_DIR)/lib/ldso/%.asm.o: $(SRC_DIR)/lib/ldso/%.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(RTLD_SO): $(BUILD_DIR)/lib/ldso/ldStart.asm.o $(BUILD_DIR)/lib/ldso/rtld.c.o
	@mkdir -p $(dir $@)
	$(CC) $(RTLD_LDFLAGS) $^ -o $@

LIB_BASE_CFLAGS := $(USER_BASE_CFLAGS) -fPIC -D_CROW_NO_POSIX_WRAPPERS $(LIB_INC_FLAGS)

LIB_DIRS := $(shell find $(SRC_DIR)/lib -mindepth 1 -maxdepth 1 -type d ! -name 'ldso' 2>/dev/null)
LIB_NAMES := $(notdir $(LIB_DIRS))
LIB_SOS := $(patsubst %, $(BUILD_DIR)/lib/%.so, $(LIB_NAMES))
LIBC_SO := $(BUILD_DIR)/lib/libc.so

define LIB_RULE
$$(BUILD_DIR)/lib/$(1)/%.c.o: $$(SRC_DIR)/lib/$(1)/%.c $$(USER_STAGED_INC)/.stamp
	@mkdir -p $$(dir $$@)
	$$(CC) $$(LIB_BASE_CFLAGS) $$(addprefix -I, $$(shell find $$(SRC_DIR)/lib/$(1) -type d 2>/dev/null)) -c $$< -o $$@

$$(BUILD_DIR)/lib/$(1)/%.asm.o: $$(SRC_DIR)/lib/$(1)/%.asm
	@mkdir -p $$(dir $$@)
	$$(AS) $$(ASFLAGS) $$< -o $$@

$$(BUILD_DIR)/lib/$(1).so: $$(if $$(filter-out libc,$(1)),$$(LIBC_SO)) \
                           $$(patsubst $$(SRC_DIR)/lib/$(1)/%.c, $$(BUILD_DIR)/lib/$(1)/%.c.o, $$(shell find $$(SRC_DIR)/lib/$(1) -name '*.c')) \
                           $$(patsubst $$(SRC_DIR)/lib/$(1)/%.asm, $$(BUILD_DIR)/lib/$(1)/%.asm.o, $$(shell find $$(SRC_DIR)/lib/$(1) -name '*.asm'))
	@mkdir -p $$(dir $$@)
	$$(CC) $$(USER_TARGET) -fuse-ld=lld -nostdlib -shared -Wl,-soname,$(1).so \
	       $$(if $$(filter-out libc,$(1)),-L$$(BUILD_DIR)/lib -lc) \
	       -Wl,-z,max-page-size=0x1000 $$(filter %.o, $$^) -o $$@
endef

$(foreach lib,$(LIB_NAMES),$(eval $(call LIB_RULE,$(lib))))

$(BUILD_DIR)/lib/libtest/%.c.o: $(SRC_DIR)/lib/libtest/%.c $(USER_STAGED_INC)/.stamp
	@mkdir -p $(dir $@)
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(LIBTEST_SO): $(LIBTEST_OBJS) $(LIBC_SO)
	@mkdir -p $(dir $@)
	$(CC) $(USER_TARGET) -fuse-ld=lld -nostdlib -shared -Wl,-soname,libtest.so -L$(BUILD_DIR)/lib -lc -Wl,-z,max-page-size=0x1000 $(filter %.o, $^) -o $@

USER_DIRS := $(shell find $(SRC_DIR)/user -mindepth 1 -maxdepth 1 -type d 2>/dev/null)

USER_NAMES := $(notdir $(USER_DIRS))
USER_ELFS := $(patsubst %, $(BUILD_DIR)/user/%.elf, $(USER_NAMES))

DYNAMIC_USER_CFLAGS = $(USER_BASE_CFLAGS) -fPIE -D_CROW_NO_POSIX_WRAPPERS $(USER_INC_FLAGS)
DYNAMIC_USER_LDFLAGS = $(USER_TARGET) -fuse-ld=lld -nostdlib -pie \
                       -Wl,--dynamic-linker=/lib/ld-crow.so \
                       -Wl,-rpath=/lib \
                       -L$(BUILD_DIR)/lib -lc \
                       -Wl,-z,max-page-size=0x1000

define USER_RULE
$$(BUILD_DIR)/user/$(1)/%.c.o: $$(SRC_DIR)/user/$(1)/%.c
	@mkdir -p $$(dir $$@)
	$$(CC) $$(DYNAMIC_USER_CFLAGS) -c $$< -o $$@

$$(BUILD_DIR)/user/$(1)/%.asm.o: $$(SRC_DIR)/user/$(1)/%.asm
	@mkdir -p $$(dir $$@)
	$$(AS) $$(ASFLAGS) $$< -o $$@

$$(BUILD_DIR)/user/$(1).elf: $(USER_CRT0) $(LIBC_SO) \
                             $$(patsubst $$(SRC_DIR)/user/$(1)/%.c, $$(BUILD_DIR)/user/$(1)/%.c.o, $$(shell find $$(SRC_DIR)/user/$(1) -name '*.c')) \
                             $$(patsubst $$(SRC_DIR)/user/$(1)/%.asm, $$(BUILD_DIR)/user/$(1)/%.asm.o, $$(shell find $$(SRC_DIR)/user/$(1) -name '*.asm'))
	@mkdir -p $$(dir $$@)
	$$(CC) $$(DYNAMIC_USER_LDFLAGS) $$(filter %.o, $$^) -o $$@
endef

$(foreach usr,$(USER_NAMES),$(eval $(call USER_RULE,$(usr))))

$(IMG): $(EFI) $(KERNEL) $(DRIVER_ELFS) $(USER_ELFS) $(RTLD_SO) $(LIB_SOS)
	dd if=/dev/zero of=$@ bs=1M count=128 status=none
ifneq ($(HAVE_PARTED),)
	parted -s $@ mklabel gpt mkpart ESP fat32 2048s 100% set 1 esp on
	mformat -i $@@@1M -F ::
	mmd -i $@@@1M ::/EFI ::/EFI/BOOT ::/EFI/novaos ::/nova ::/nova/drivers ::/nova/fw ::/bin ::/lib
	mcopy -i $@@@1M $(EFI) ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $@@@1M $(KERNEL) ::/EFI/novaos/$(KERNEL)
	mcopy -i $@@@1M zap-light16.psf ::/EFI/novaos/zap-light16.psf
	mcopy -i $@@@1M reallylongfilenamecros.txt ::/EFI/novaos/reallylongfilenamecros.txt
	mcopy -i $@@@1M $(RTLD_SO) ::/lib/ld-crow.so
	@for so in $(LIB_SOS); do \
		if [ -f "$$so" ]; then \
			mcopy -i $@@@1M "$$so" ::/lib/$$(basename "$$so"); \
		fi \
	done
	## PLEASE DO NOT REMOVE ANY FILES FROM HERE
	@for fw in firmware/nvidia/*.bin; do \
		if [ -f "$$fw" ]; then \
			mcopy -i $@@@1M "$$fw" ::/nova/fw/$$(basename "$$fw"); \
		fi \
	done
	## TO HERE
	@for drv in $(DRIVER_ELFS); do \
		if [ -f "$$drv" ]; then \
			mcopy -i $@@@1M "$$drv" ::/nova/drivers/$$(basename "$$drv"); \
		fi \
	done
	@for usr in $(USER_ELFS); do \
		if [ -f "$$usr" ]; then \
			mcopy -i $@@@1M "$$usr" ::/EFI/novaos/$$(basename "$$usr"); \
			mcopy -i $@@@1M "$$usr" ::/bin/$$(basename "$$usr"); \
		fi \
	done
else
	mformat -i $@ -F ::
	mmd -i $@ ::/EFI ::/EFI/BOOT ::/EFI/novaos ::/nova ::/nova/drivers ::/nova/fw ::/bin ::/lib
	mcopy -i $@ $(EFI) ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $@ $(KERNEL) ::/EFI/novaos/$(KERNEL)
	mcopy -i $@ zap-light16.psf ::/EFI/novaos/zap-light16.psf
	mcopy -i $@ reallylongfilenamecros.txt ::/EFI/novaos/reallylongfilenamecros.txt
	mcopy -i $@ $(RTLD_SO) ::/lib/ld-crow.so
	@for so in $(LIB_SOS); do \
		if [ -f "$$so" ]; then \
			mcopy -i $@ "$$so" ::/lib/$$(basename "$$so"); \
		fi \
	done
	## PLEASE DO NOT REMOVE ANY FILES FROM HERE
	@for fw in firmware/nvidia/*.bin; do \
		if [ -f "$$fw" ]; then \
			mcopy -i $@ "$$fw" ::/nova/fw/$$(basename "$$fw"); \
		fi \
	done
	## TO HERE
	@for drv in $(DRIVER_ELFS); do \
		if [ -f "$$drv" ]; then \
			mcopy -i $@ "$$drv" ::/nova/drivers/$$(basename "$$drv"); \
		fi \
	done
	@for usr in $(USER_ELFS); do \
		if [ -f "$$usr" ]; then \
			mcopy -i $@ "$$usr" ::/EFI/novaos/$$(basename "$$usr"); \
			mcopy -i $@ "$$usr" ::/bin/$$(basename "$$usr"); \
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
	-vga none -device virtio-vga-gl,hostmem=1G $(QEMU_CPU) $(QEMU_ACCEL) -M q35 -display gtk,gl=on

run-vfio: $(IMG)
	sudo qemu-system-x86_64 -m 1G -bios $(OVMF) -drive file=$(IMG),format=raw,if=none,id=nvm0 -device nvme,serial=1234ffff,drive=nvm0 $(QEMU_CPU) $(QEMU_ACCEL) -M q35 \
	-device pcie-root-port,id=root_port1,chassis=1,slot=1,bus=pcie.0 -device vfio-pci,host=01:00.0,bus=root_port1,multifunction=on -serial stdio
	reset
clean:
	rm -rf $(BUILD_DIR) $(EFI) $(KERNEL) $(IMG)

install: $(EFI) $(KERNEL) $(DRIVER_ELFS) $(USER_ELFS) $(RTLD_SO) $(LIB_SOS)
	sudo mkdir -p $(INSTALL_DIR)
	sudo cp $(EFI) $(INSTALL_DIR)/$(EFI)
	sudo cp $(KERNEL) $(INSTALL_DIR)/$(KERNEL)
	sudo cp zap-light16.psf $(INSTALL_DIR)/zap-light16.psf
	sudo mkdir -p $(ESP_PATH)/nova/drivers
	sudo mkdir -p $(ESP_PATH)/lib
	sudo cp $(RTLD_SO) $(ESP_PATH)/lib/ld-crow.so
	@for so in $(LIB_SOS); do \
		if [ -f "$$so" ]; then \
			sudo cp "$$so" $(ESP_PATH)/lib/$$(basename "$$so"); \
		fi \
	done
	@for drv in $(DRIVER_ELFS); do \
		if [ -f "$$drv" ]; then \
			sudo cp "$$drv" $(ESP_PATH)/nova/drivers/$$(basename "$$drv"); \
		fi \
	done
	@for usr in $(USER_ELFS); do \
		if [ -f "$$usr" ]; then \
			sudo cp "$$usr" $(INSTALL_DIR)/$$(basename "$$usr"); \
		fi \
	done
run-hw: install
	sudo efibootmgr -n $(GRUB_BOOTNUM)
	sudo grub-reboot "nova_os"
	systemctl reboot

.PHONY: all run clean install run-hw
