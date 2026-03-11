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

#define PAGE_SIZE 0x1000ULL
#define KERNEL_MIN_LOAD_ADDR 0x2000000ULL

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

static UINT64 align_down(UINT64 v, UINT64 a) {
    return v & ~(a - 1);
}

static UINT64 align_up(UINT64 v, UINT64 a) {
    return (v + a - 1) & ~(a - 1);
}

static void fatal(EFI_SYSTEM_TABLE* SystemTable, CHAR16* msg, EFI_STATUS status) {
    Print(L"%s: %r\r\n", msg, status);
    dbg_putc('!');
    for (;;) {
        uefi_call_wrapper(SystemTable->BootServices->Stall, 1, 1000000);
    }
}

static EFI_STATUS get_memory_map_alloc(
    EFI_SYSTEM_TABLE* SystemTable,
    EFI_MEMORY_DESCRIPTOR** mmap_out,
    UINTN* mmap_size_out,
    UINTN* map_key_out,
    UINTN* desc_size_out,
    UINT32* desc_version_out
) {
    EFI_STATUS status;
    EFI_MEMORY_DESCRIPTOR* mmap = NULL;
    UINTN mmap_size = 0;
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
        return status;
    }

    mmap_size += desc_size * 16;

    status = uefi_call_wrapper(
        SystemTable->BootServices->AllocatePool,
        3,
        EfiLoaderData,
        mmap_size,
        (void**)&mmap
    );
    if (EFI_ERROR(status)) {
        return status;
    }

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
            uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, mmap);

            mmap_size = current_size + desc_size * 16;

            status = uefi_call_wrapper(
                SystemTable->BootServices->AllocatePool,
                3,
                EfiLoaderData,
                mmap_size,
                (void**)&mmap
            );
            if (EFI_ERROR(status)) {
                return status;
            }

            dbg_putc('R');
            continue;
        }

        if (EFI_ERROR(status)) {
            uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, mmap);
            return status;
        }

        *mmap_out = mmap;
        *mmap_size_out = current_size;
        *map_key_out = map_key;
        *desc_size_out = desc_size;
        *desc_version_out = desc_version;
        return EFI_SUCCESS;
    }
}

static EFI_STATUS find_free_region(
    EFI_MEMORY_DESCRIPTOR* mmap,
    UINTN mmap_size,
    UINTN desc_size,
    UINT64 min_addr,
    UINT64 size_needed,
    EFI_PHYSICAL_ADDRESS* out_addr
) {
    UINTN count = mmap_size / desc_size;

    for (UINTN i = 0; i < count; i++) {
        EFI_MEMORY_DESCRIPTOR* desc =
            (EFI_MEMORY_DESCRIPTOR*)((UINT8*)mmap + i * desc_size);

        if (desc->Type != EfiConventionalMemory) {
            continue;
        }

        UINT64 region_start = desc->PhysicalStart;
        UINT64 region_end = desc->PhysicalStart + desc->NumberOfPages * PAGE_SIZE;

        if (region_end <= min_addr) {
            continue;
        }

        UINT64 candidate = region_start;
        if (candidate < min_addr) {
            candidate = min_addr;
        }

        candidate = align_up(candidate, PAGE_SIZE);

        if (candidate + size_needed <= region_end) {
            *out_addr = (EFI_PHYSICAL_ADDRESS)candidate;
            return EFI_SUCCESS;
        }
    }

    return EFI_NOT_FOUND;
}

static EFI_STATUS load_kernel_from_blob(
    EFI_SYSTEM_TABLE* SystemTable,
    EFI_MEMORY_DESCRIPTOR* mmap,
    UINTN mmap_size,
    UINTN desc_size,
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

    Elf64_Addr min_vaddr = ~0ULL;
    Elf64_Addr max_vaddr = 0;

    for (Elf64_Half i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr* ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;

        dbg_putc('3');

        if (ph->p_vaddr < min_vaddr) {
            min_vaddr = ph->p_vaddr;
        }

        if (ph->p_vaddr + ph->p_memsz > max_vaddr) {
            max_vaddr = ph->p_vaddr + ph->p_memsz;
        }
    }

    if (min_vaddr == ~0ULL || max_vaddr <= min_vaddr) {
        return EFI_LOAD_ERROR;
    }

    UINT64 image_base = align_down(min_vaddr, PAGE_SIZE);
    UINT64 image_end  = align_up(max_vaddr, PAGE_SIZE);
    UINT64 image_size = image_end - image_base;

    EFI_PHYSICAL_ADDRESS load_base = 0;
    status = find_free_region(
        mmap,
        mmap_size,
        desc_size,
        KERNEL_MIN_LOAD_ADDR,
        image_size,
        &load_base
    );
    if (EFI_ERROR(status)) {
        Print(L"find_free_region failed: %r\r\n", status);
        return status;
    }

    Print(L"kernel image base=%lx size=%lx chosen=%lx\r\n", image_base, image_size, load_base);

    EFI_PHYSICAL_ADDRESS alloc_addr = load_base;
    dbg_putc('4');

    status = uefi_call_wrapper(
        SystemTable->BootServices->AllocatePages,
        4,
        AllocateAddress,
        EfiLoaderData,
        EFI_SIZE_TO_PAGES(image_size),
        &alloc_addr
    );
    if (EFI_ERROR(status)) {
        Print(L"AllocatePages failed: %r addr=%lx pages=%lu\r\n",
              status, alloc_addr, EFI_SIZE_TO_PAGES(image_size));
        return status;
    }

    uefi_call_wrapper(
        SystemTable->BootServices->SetMem,
        3,
        (void*)(UINTN)alloc_addr,
        (UINTN)image_size,
        0
    );

    INT64 slide = (INT64)load_base - (INT64)image_base;
    Print(L"kernel slide=%lx\r\n", (UINT64)slide);

    for (Elf64_Half i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr* ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;

        UINT64 dst = (UINT64)((INT64)ph->p_vaddr + slide);

        dbg_putc('5');

        uefi_call_wrapper(
            SystemTable->BootServices->CopyMem,
            3,
            (void*)(UINTN)dst,
            (void*)((UINT8*)kernel_buffer + ph->p_offset),
            (UINTN)ph->p_filesz
        );
    }

    *entry_out = (kernel_entry_raw_t)(UINTN)((INT64)ehdr->e_entry + slide);

    dbg_putc('6');
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

    EFI_MEMORY_DESCRIPTOR* mmap = NULL;
    UINTN mmap_size = 0;
    UINTN map_key = 0;
    UINTN desc_size = 0;
    UINT32 desc_version = 0;

    status = get_memory_map_alloc(
        SystemTable,
        &mmap,
        &mmap_size,
        &map_key,
        &desc_size,
        &desc_version
    );
    if (EFI_ERROR(status)) {
        fatal(SystemTable, L"Get memory map before kernel load failed", status);
    }

    dbg_putc('M');

    kernel_entry_raw_t kernel_entry = NULL;
    status = load_kernel_from_blob(
        SystemTable,
        mmap,
        mmap_size,
        desc_size,
        &kernel_entry
    );
    if (EFI_ERROR(status)) {
        fatal(SystemTable, L"Kernel load failed", status);
    }

    dbg_putc('F');

    uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, mmap);
    mmap = NULL;

    status = get_memory_map_alloc(
        SystemTable,
        &mmap,
        &mmap_size,
        &map_key,
        &desc_size,
        &desc_version
    );
    if (EFI_ERROR(status)) {
        fatal(SystemTable, L"Get memory map before ExitBootServices failed", status);
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
        Print(L"ExitBootServices failed: %r\r\n", status);
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