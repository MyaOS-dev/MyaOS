BUILD := build
EFI_BOOT := BOOTX64.EFI
KERNEL := kernel.elf
ESP_IMAGE := esp.img

CC := clang
LD_KERNEL := ld.lld
LD_EFI := ld.bfd
OBJCOPY := objcopy
NASM := nasm

CFLAGS_KERNEL := -target x86_64-elf -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -mcmodel=large -Wall -Wextra
LDFLAGS_KERNEL := -nostdlib -z max-page-size=0x1000 -T linker.ld

CFLAGS_EFI := -I/usr/include/efi -I/usr/include/efi/x86_64 -ffreestanding -fpic -fshort-wchar -fno-stack-protector -mno-red-zone -Wall -Wextra
LDFLAGS_EFI := -nostdlib -shared -Bsymbolic -znocombreloc -m elf_x86_64 \
	-T /usr/lib/elf_x86_64_efi.lds \
	/usr/lib/crt0-efi-x86_64.o

all: $(BUILD)/$(EFI_BOOT) $(BUILD)/$(ESP_IMAGE)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/entry.o: kernel/entry.asm | $(BUILD)
	$(NASM) -f elf64 kernel/entry.asm -o $(BUILD)/entry.o

$(BUILD)/kernel.o: kernel/kernel.c kernel/boot.h kernel/graphics.h | $(BUILD)
	$(CC) $(CFLAGS_KERNEL) -c kernel/kernel.c -o $(BUILD)/kernel.o

$(BUILD)/graphics.o: kernel/graphics.c kernel/boot.h kernel/graphics.h | $(BUILD)
	$(CC) $(CFLAGS_KERNEL) -c kernel/graphics.c -o $(BUILD)/graphics.o

$(BUILD)/$(KERNEL): $(BUILD)/entry.o $(BUILD)/kernel.o $(BUILD)/graphics.o linker.ld
	$(LD_KERNEL) $(LDFLAGS_KERNEL) \
		$(BUILD)/entry.o \
		$(BUILD)/kernel.o \
		$(BUILD)/graphics.o \
		-o $(BUILD)/$(KERNEL)

$(BUILD)/kernel_blob.o: $(BUILD)/$(KERNEL)
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
		$(BUILD)/$(KERNEL) $(BUILD)/kernel_blob.o

$(BUILD)/boot.o: boot/boot.c kernel/boot.h | $(BUILD)
	$(CC) $(CFLAGS_EFI) -DEFI_FUNCTION_WRAPPER -c boot/boot.c -o $(BUILD)/boot.o

$(BUILD)/boot.so: $(BUILD)/boot.o $(BUILD)/kernel_blob.o
	$(LD_EFI) $(LDFLAGS_EFI) \
		$(BUILD)/boot.o \
		$(BUILD)/kernel_blob.o \
		-L /usr/lib -lefi -lgnuefi \
		-o $(BUILD)/boot.so

$(BUILD)/$(EFI_BOOT): $(BUILD)/boot.so
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
		$(BUILD)/boot.so $(BUILD)/$(EFI_BOOT)

$(BUILD)/$(ESP_IMAGE): $(BUILD)/$(EFI_BOOT)
	rm -f $(BUILD)/$(ESP_IMAGE)
	dd if=/dev/zero of=$(BUILD)/$(ESP_IMAGE) bs=1M count=64
	mkfs.fat -F 32 $(BUILD)/$(ESP_IMAGE)
	mmd -i $(BUILD)/$(ESP_IMAGE) ::/EFI
	mmd -i $(BUILD)/$(ESP_IMAGE) ::/EFI/BOOT
	mcopy -i $(BUILD)/$(ESP_IMAGE) $(BUILD)/$(EFI_BOOT) ::/EFI/BOOT/BOOTX64.EFI

run: all
	qemu-system-x86_64 \
		-machine q35 \
		-m 512M \
		-no-reboot \
		-no-shutdown \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd \
		-drive if=pflash,format=raw,file=/usr/share/edk2/x64/OVMF_VARS.4m.fd \
		-drive file=$(BUILD)/$(ESP_IMAGE),format=raw

debug: all
	qemu-system-x86_64 \
		-machine q35 \
		-m 512M \
		-no-reboot \
		-no-shutdown \
		-debugcon stdio \
		-global isa-debugcon.iobase=0xe9 \
		-d int,cpu_reset,guest_errors \
		-D qemu.log \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd \
		-drive if=pflash,format=raw,file=/usr/share/edk2/x64/OVMF_VARS.4m.fd \
		-drive file=$(BUILD)/$(ESP_IMAGE),format=raw

clean:
	rm -rf $(BUILD)

.PHONY: all run debug clean
