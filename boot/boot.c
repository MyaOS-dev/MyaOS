#include <efi.h>
#include <efilib.h>
#include "../kernel/boot.h"
typedef unsigned char  Elf64_Byte;
typedef uint16_t       Elf64_Half;
typedef uint32_t       Elf64_Word;
typedef uint64_t       Elf64_Xword;
typedef uint64_t       Elf64_Addr;
typedef uint64_t       Elf64_Off;
#define PT_LOAD 1
#define ELFCLASS64 2
#define EM_X86_64 62
typedef struct {
    Elf64_Byte  e_ident[16];
    Elf64_Half  e_type;
    Elf64_Half  e_machine;
    Elf64_Word  e_version;
    Elf64_Addr  e_entry;
    Elf64_Off   e_phoff;
    Elf64_Off   e_shoff;
    Elf64_Word  e_flags;
    Elf64_Half  e_ehsize;
    Elf64_Half  e_phentsize;
    Elf64_Half  e_phnum;
    Elf64_Half  e_shentsize;
    Elf64_Half  e_shnum;
    Elf64_Half  e_shstrndx;
} Elf64_Ehdr;
typedef struct {
    Elf64_Word  p_type;
    Elf64_Word  p_flags;
    Elf64_Off   p_offset;
    Elf64_Addr  p_vaddr;
    Elf64_Addr  p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
} Elf64_Phdr;
typedef void (*kernel_entry_raw_t)(boot_info_t*);
static EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
extern unsigned char _binary_build_kernel_elf_start[];
extern unsigned char _binary_build_kernel_elf_end[];
static inline void dbg_putc(char c) {
    __asm__ __volatile__("outb %0, $0xe9" : : "a"(c));
}
static void fatal(EFI_SYSTEM_TABLE* SystemTable, CHAR16* msg, EFI_STATUS status) {
    Print(L"%s: %r\r\n", msg, status);
    dbg_putc('!');
    for (;;) {
        uefi_call_wrapper(SystemTable->BootServices->Stall, 1, 1000000);
    }
}
static EFI_STATUS load_kernel_from_blob(
    EFI_SYSTEM_TABLE* SystemTable,
    kernel_entry_raw_t* entry_out
) {
    EFI_STATUS status;
    void* kernel_buffer = (void*)_binary_build_kernel_elf_start;
    UINTN kernel_size = (UINTN)(_binary_build_kernel_elf_end - _binary_build_kernel_elf_start);
    (void)kernel_size;
    dbg_putc('1');
    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)kernel_buffer;
    if (!(ehdr->e_ident[0] == 0x7F &&
          ehdr->e_ident[1] == 'E' &&
          ehdr->e_ident[2] == 'L' &&
          ehdr->e_ident[3] == 'F')) {
        return EFI_LOAD_ERROR;
    }
    if (ehdr->e_ident[4] != ELFCLASS64) {
        return EFI_LOAD_ERROR;
    }
    if (ehdr->e_machine != EM_X86_64) {
        return EFI_LOAD_ERROR;
    }
    dbg_putc('2');
    Elf64_Phdr* phdrs = (Elf64_Phdr*)((UINT8*)kernel_buffer + ehdr->e_phoff);
    for (Elf64_Half i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr* ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;
        EFI_PHYSICAL_ADDRESS segment = (EFI_PHYSICAL_ADDRESS)ph->p_vaddr;
        UINTN pages = EFI_SIZE_TO_PAGES((UINTN)ph->p_memsz);
        dbg_putc('3');
        status = uefi_call_wrapper(
            SystemTable->BootServices->AllocatePages,
            4,
            AllocateAddress,
            EfiLoaderData,
            pages,
            &segment
        );
        if (EFI_ERROR(status)) return status;
        uefi_call_wrapper(
            SystemTable->BootServices->SetMem,
            3,
            (void*)(UINTN)segment,
            (UINTN)ph->p_memsz,
            0
        );
        uefi_call_wrapper(
            SystemTable->BootServices->CopyMem,
            3,
            (void*)(UINTN)segment,
            (void*)((UINT8*)kernel_buffer + ph->p_offset),
            (UINTN)ph->p_filesz
        );
    }
    dbg_putc('4');
    *entry_out = (kernel_entry_raw_t)(UINTN)ehdr->e_entry;
    return EFI_SUCCESS;
}
EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    dbg_putc('A');
    EFI_STATUS status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;
    status = uefi_call_wrapper(
        SystemTable->BootServices->LocateProtocol,
        3,
        &gop_guid,
        NULL,
        (void**)&gop
    );
    if (EFI_ERROR(status)) {
        fatal(SystemTable, L"GOP not found", status);
    }
    dbg_putc('B');
    status = uefi_call_wrapper(
        gop->SetMode,
        2,
        gop,
        gop->Mode->Mode
    );
    if (EFI_ERROR(status)) {
        fatal(SystemTable, L"SetMode failed", status);
    }
    dbg_putc('C');
    framebuffer_t* fb_ptr = NULL;
    status = uefi_call_wrapper(
        SystemTable->BootServices->AllocatePool,
        3,
        EfiLoaderData,
        sizeof(framebuffer_t),
        (void**)&fb_ptr
    );
    if (EFI_ERROR(status)) {
        fatal(SystemTable, L"Framebuffer struct alloc failed", status);
    }
    dbg_putc('D');
    fb_ptr->base = gop->Mode->FrameBufferBase;
    fb_ptr->size = gop->Mode->FrameBufferSize;
    fb_ptr->width = gop->Mode->Info->HorizontalResolution;
    fb_ptr->height = gop->Mode->Info->VerticalResolution;
    fb_ptr->pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine;
    fb_ptr->format = gop->Mode->Info->PixelFormat;
    dbg_putc('E');
    boot_info_t* boot_info = NULL;
    status = uefi_call_wrapper(
        SystemTable->BootServices->AllocatePool,
        3,
        EfiLoaderData,
        sizeof(boot_info_t),
        (void**)&boot_info
    );
    if (EFI_ERROR(status)) {
        fatal(SystemTable, L"boot_info alloc failed", status);
    }
    dbg_putc('K');
    kernel_entry_raw_t kernel_entry = NULL;
    status = load_kernel_from_blob(SystemTable, &kernel_entry);
    if (EFI_ERROR(status)) {
        fatal(SystemTable, L"Kernel load failed", status);
    }
    dbg_putc('F');
    UINTN mmap_size = 0;
    EFI_MEMORY_DESCRIPTOR* mmap = NULL;
    UINTN map_key = 0;
    UINTN desc_size = 0;
    UINT32 desc_version = 0;
    status = uefi_call_wrapper(
        SystemTable->BootServices->GetMemoryMap,
        5,
        &mmap_size,
        mmap,
        &map_key,
        &desc_size,
        &desc_version
    );
    if (status != EFI_BUFFER_TOO_SMALL) {
        fatal(SystemTable, L"GetMemoryMap #1 failed", status);
    }
    dbg_putc('G');
    mmap_size += desc_size * 16;
    status = uefi_call_wrapper(
        SystemTable->BootServices->AllocatePool,
        3,
        EfiLoaderData,
        mmap_size,
        (void**)&mmap
    );
    if (EFI_ERROR(status)) {
        fatal(SystemTable, L"AllocatePool for mmap failed", status);
    }
    dbg_putc('H');
    for (;;) {
        UINTN current_size = mmap_size;
        status = uefi_call_wrapper(
            SystemTable->BootServices->GetMemoryMap,
            5,
            &current_size,
            mmap,
            &map_key,
            &desc_size,
            &desc_version
        );
        if (status == EFI_BUFFER_TOO_SMALL) {
            status = uefi_call_wrapper(
                SystemTable->BootServices->FreePool,
                1,
                mmap
            );
            if (EFI_ERROR(status)) {
                fatal(SystemTable, L"FreePool failed", status);
            }
            mmap_size = current_size + desc_size * 16;
            status = uefi_call_wrapper(
                SystemTable->BootServices->AllocatePool,
                3,
                EfiLoaderData,
                mmap_size,
                (void**)&mmap
            );
            if (EFI_ERROR(status)) {
                fatal(SystemTable, L"Re-AllocatePool failed", status);
            }
            dbg_putc('R');
            continue;
        }
        if (EFI_ERROR(status)) {
            fatal(SystemTable, L"GetMemoryMap #2 failed", status);
        }
        mmap_size = current_size;
        break;
    }
    dbg_putc('I');
    boot_info->fb = *fb_ptr;
    boot_info->mmap = (uint64_t)(UINTN)mmap;
    boot_info->mmap_size = (uint64_t)mmap_size;
    boot_info->desc_size = (uint64_t)desc_size;
    dbg_putc('L');
    status = uefi_call_wrapper(
        SystemTable->BootServices->ExitBootServices,
        2,
        ImageHandle,
        map_key
    );
    if (EFI_ERROR(status)) {
        dbg_putc('X');
        for (;;) {
            __asm__ __volatile__("cli; hlt");
        }
    }
    dbg_putc('J');
    kernel_entry(boot_info);
    dbg_putc('Z');
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
    return EFI_SUCCESS;
}