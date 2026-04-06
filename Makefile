BUILD := build
EFI_BOOT := BOOTX64.EFI
KERNEL := kernel.elf
ESP_IMAGE := esp.img
OVMF_CODE := /usr/share/edk2/x64/OVMF_CODE.4m.fd
OVMF_VARS_TEMPLATE := /usr/share/edk2/x64/OVMF_VARS.4m.fd
OVMF_VARS := $(BUILD)/OVMF_VARS.4m.fd
MYAOS_DEBUG ?= 0
ARCH ?= x86_64

CC := clang
LD_KERNEL := ld.lld
LD_EFI := ld.bfd
NASM := nasm
OBJCOPY := objcopy
GDB := gdb

KERNEL_INCLUDES := -Iinclude -Ikernel/core -Ikernel/dev -Ikernel/fs -Ikernel/gfx -Ikernel/mm -Ikernel/proc -Ikernel/net -Ikernel/arch/$(ARCH)
ifeq ($(MYAOS_DEBUG),1)
DEBUG_CFLAGS := -g3 -O0 -fno-omit-frame-pointer
else
DEBUG_CFLAGS := -O2
endif
CFLAGS_KERNEL := -target x86_64-elf -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -mcmodel=large -mstackrealign -Wall -Wextra $(DEBUG_CFLAGS) -DMYAOS_DEBUG=$(MYAOS_DEBUG) $(KERNEL_INCLUDES)
LDFLAGS_KERNEL := -nostdlib -z max-page-size=0x1000 -T linker.ld

CFLAGS_EFI := -I/usr/include/efi -I/usr/include/efi/x86_64 -ffreestanding -fpic -fshort-wchar -fno-stack-protector -mno-red-zone -Wall -Wextra
LDFLAGS_EFI := -nostdlib -shared -Bsymbolic -znocombreloc -m elf_x86_64 \
	-T /usr/lib/elf_x86_64_efi.lds \
	/usr/lib/crt0-efi-x86_64.o

CFLAGS_PROGRAM := -target x86_64-elf -ffreestanding -fpie -fno-stack-protector -mno-red-zone -mcmodel=small -Wall -Wextra $(DEBUG_CFLAGS) -Iinclude -Iprograms/lib
LDFLAGS_PROGRAM := -nostdlib -Wl,-T,programs/program.ld -Wl,-e,program_main -Wl,-pie
CFLAGS_SHARED := -target x86_64-elf -ffreestanding -fpic -fno-stack-protector -mno-red-zone -mcmodel=small -Wall -Wextra $(DEBUG_CFLAGS) -Iinclude
LDFLAGS_SHARED := -nostdlib -Wl,-shared -Wl,-e,0

KERNEL_C_SOURCES := $(sort \
	$(wildcard kernel/core/*.c) \
	$(wildcard kernel/dev/*.c) \
	$(wildcard kernel/fs/*.c) \
	$(wildcard kernel/gfx/*.c) \
	$(wildcard kernel/mm/*.c) \
	$(wildcard kernel/net/*.c) \
	$(wildcard kernel/proc/*.c) \
	$(wildcard kernel/arch/$(ARCH)/*.c))

ifeq ($(ARCH),x86_64)
KERNEL_ASM_SOURCES := kernel/arch/x86_64/entry.asm kernel/arch/x86_64/interrupts.asm
KERNEL_ASM_OBJECTS := $(BUILD)/kernel/arch/x86_64/entry_asm.o $(BUILD)/kernel/arch/x86_64/interrupts_asm.o
else
KERNEL_ASM_SOURCES := kernel/arch/x86_64/entry.asm
KERNEL_ASM_OBJECTS := $(BUILD)/kernel/arch/x86_64/entry_asm.o
endif
KERNEL_C_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(KERNEL_C_SOURCES))
KERNEL_OBJECTS := $(KERNEL_C_OBJECTS) $(KERNEL_ASM_OBJECTS)

PROGRAM_SOURCES := \
	programs/cate.c \
	programs/system/about.c \
	programs/system/add.c \
	programs/system/msh.c \
	programs/system/cls.c \
	programs/system/say.c \
	programs/system/stop.c \
	programs/system/restart.c \
	programs/system/poweroff.c \
	programs/system/pause.c \
	programs/system/ok.c \
	programs/system/fail.c \
	programs/system/syscfg.c \
	programs/system/whoami.c \
	programs/system/login.c \
	programs/system/abi.c \
	programs/system/pkg.c \
	programs/system/update.c \
	programs/system/modctl.c \
	programs/system/hotplug.c \
	programs/system/compat.c \
	programs/system/uname.c \
	programs/system/kill.c \
	programs/fs/list.c \
	programs/fs/show.c \
	programs/fs/del.c \
	programs/fs/mkfolder.c \
	programs/fs/attach.c \
	programs/fs/newfile.c \
	programs/fs/save.c \
	programs/fs/whereami.c \
	programs/fs/attached.c \
	programs/debug/disks.c \
	programs/debug/cpuburn.c \
	programs/debug/limitcheck.c \
	programs/debug/meminfo.c \
	programs/debug/notifycheck.c \
	programs/debug/sched.c \
	programs/debug/schedcheck.c \
	programs/debug/seccheck.c \
	programs/debug/tasks.c \
	programs/debug/devls.c \
	programs/debug/fbinfo.c \
	programs/debug/netstat.c \
	programs/debug/swapstat.c \
	programs/debug/swapcheck.c \
	programs/debug/posixcheck.c \
	programs/debug/dlcheck.c \
	programs/text/count.c \
	programs/text/less.c \
	programs/text/findtext.c \
	programs/text/firstlines.c \
	programs/ipc/notify.c \
	programs/ipc/notifypoll.c \
	programs/ipc/notifywait.c \
	programs/ipc/msgsend.c \
	programs/ipc/msgrecv.c \
	programs/ipc/pipecheck.c \
	programs/ipc/shmcheck.c \
	programs/ipc/sockcheck.c \
	programs/ipc/tcpchk.c \
	programs/ipc/threadcheck.c \
	programs/net/netsend.c \
	programs/net/netrecv.c \
	programs/net/tcpsend.c \
	programs/net/tcprecv.c \
	programs/net/udp4send.c

PROGRAM_BINS := $(patsubst programs/%.c,$(BUILD)/programs/%.elf,$(PROGRAM_SOURCES))
LIB_SOURCES := \
	programs/shared/libdemo.c
LIB_BINS := $(patsubst programs/shared/%.c,$(BUILD)/lib/%.so,$(LIB_SOURCES))
PROGRAM_MANIFESTS := $(sort $(wildcard programs/cmd/*.cmd))
AUTORUN_SCRIPT := programs/autorun.sh

all: $(BUILD)/$(EFI_BOOT) $(BUILD)/$(KERNEL) $(PROGRAM_BINS) $(LIB_BINS) $(BUILD)/$(ESP_IMAGE)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/kernel/%.o: kernel/%.c Makefile | $(BUILD)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

$(BUILD)/kernel/arch/x86_64/entry_asm.o: kernel/arch/x86_64/entry.asm Makefile | $(BUILD)
	mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

$(BUILD)/kernel/arch/x86_64/interrupts_asm.o: kernel/arch/x86_64/interrupts.asm Makefile | $(BUILD)
	mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

$(BUILD)/$(KERNEL): $(KERNEL_OBJECTS) linker.ld Makefile | $(BUILD)
	$(LD_KERNEL) $(LDFLAGS_KERNEL) $(KERNEL_OBJECTS) -o $@

$(BUILD)/boot/boot.o: boot/boot.c kernel/core/boot.h Makefile | $(BUILD)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS_EFI) -DEFI_FUNCTION_WRAPPER -c $< -o $@

$(BUILD)/boot/boot.so: $(BUILD)/boot/boot.o Makefile | $(BUILD)
	mkdir -p $(dir $@)
	$(LD_EFI) $(LDFLAGS_EFI) \
		$(BUILD)/boot/boot.o \
		-L /usr/lib -lefi -lgnuefi \
		-o $@

$(BUILD)/$(EFI_BOOT): $(BUILD)/boot/boot.so Makefile | $(BUILD)
	$(OBJCOPY) \
		-j .text \
		-j .sdata \
		-j .data \
		-j .dynamic \
		-j .dynsym \
		-j .rel \
		-j .rela \
		-j .reloc \
		-O efi-app-x86_64 \
		$< $@

$(BUILD)/programs/%.elf: programs/%.c programs/lib/myaos.h programs/program.ld include/myaos/syscall.h Makefile | $(BUILD)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS_PROGRAM) $(LDFLAGS_PROGRAM) $< -o $@

$(BUILD)/lib/%.so: programs/shared/%.c Makefile | $(BUILD)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS_SHARED) $(LDFLAGS_SHARED) $< -o $@

$(BUILD)/$(ESP_IMAGE): $(BUILD)/$(EFI_BOOT) $(BUILD)/$(KERNEL) $(PROGRAM_BINS) $(LIB_BINS) $(PROGRAM_MANIFESTS) $(AUTORUN_SCRIPT) | $(BUILD)
	rm -f $@
	dd if=/dev/zero of=$@ bs=1M count=64
	mkfs.fat -F 32 $@
	mmd -i $@ ::/EFI
	mmd -i $@ ::/EFI/BOOT
	mmd -i $@ ::/bin
	mmd -i $@ ::/lib
	mmd -i $@ ::/cmd
	mcopy -i $@ $(BUILD)/$(EFI_BOOT) ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $@ $(BUILD)/$(KERNEL) ::/kernel.elf
	mcopy -i $@ $(BUILD)/$(KERNEL) ::/EFI/BOOT/kernel.elf
	mcopy -i $@ $(AUTORUN_SCRIPT) ::/autorun.sh
	for prog in $(PROGRAM_BINS); do mcopy -i $@ $$prog ::/bin/$$(basename $$prog); done
	for lib in $(LIB_BINS); do mcopy -i $@ $$lib ::/lib/$$(basename $$lib); done
	for manifest in $(PROGRAM_MANIFESTS); do mcopy -i $@ $$manifest ::/cmd/$$(basename $$manifest); done

run: all $(OVMF_VARS)
	qemu-system-x86_64 \
		-machine q35 \
		-m 1G \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive if=pflash,format=raw,file=$(OVMF_VARS) \
		-drive file=$(BUILD)/$(ESP_IMAGE),format=raw

debug: all $(OVMF_VARS)
	qemu-system-x86_64 \
		-machine q35 \
		-m 1G \
		-no-reboot \
		-no-shutdown \
		-debugcon stdio \
		-global isa-debugcon.iobase=0xe9 \
		-d int,cpu_reset,guest_errors \
		-D qemu.log \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive if=pflash,format=raw,file=$(OVMF_VARS) \
		-drive file=$(BUILD)/$(ESP_IMAGE),format=raw

debug-shell: all $(OVMF_VARS)
	qemu-system-x86_64 \
		-machine q35 \
		-m 1G \
		-no-reboot \
		-no-shutdown \
		-debugcon stdio \
		-global isa-debugcon.iobase=0xe9 \
		-d int,cpu_reset,guest_errors \
		-D qemu.log \
		-s -S \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive if=pflash,format=raw,file=$(OVMF_VARS) \
		-drive file=$(BUILD)/$(ESP_IMAGE),format=raw

gdb-shell: $(BUILD)/$(KERNEL)
	$(GDB) -x scripts/shell-debug.gdb

test: all
	./scripts/test_smoke.sh

test-runtime: all $(OVMF_VARS)
	./scripts/test_runtime.sh

myafs-driver:
	./tools/myafs_driver/build.sh

install-mkfs-myafs: myafs-driver
	install -Dm755 tools/myafs_driver/mkfs.myafs /usr/local/sbin/mkfs.myafs

myafs-host-kmod:
	$(MAKE) -C tools/myafs_host_linux

myafs-host-dkms-install:
	tools/myafs_host_linux/dkms-install.sh

myafs-host-dkms-remove:
	tools/myafs_host_linux/dkms-remove.sh

clean:
	rm -rf $(BUILD)
	rm -f tools/myafs_driver/myafsdrv tools/myafs_driver/myafsdrv.exe
	rm -f tools/myafs_driver/mkfs.myafs tools/myafs_driver/mkfs.myafs.exe
	$(MAKE) -C tools/myafs_host_linux clean || true

.PHONY: all run debug debug-shell gdb-shell test test-runtime myafs-driver install-mkfs-myafs myafs-host-kmod myafs-host-dkms-install myafs-host-dkms-remove clean
$(OVMF_VARS): | $(BUILD)
	cp $(OVMF_VARS_TEMPLATE) $@

debug debug-shell gdb-shell: MYAOS_DEBUG=1
