#include <efi.h>
#include <efilib.h>
#include <efidevp.h>
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
#define BOOT_VOLUME_COPY_LIMIT (128ULL * 1024ULL * 1024ULL)

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
static EFI_GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID block_io_guid = EFI_BLOCK_IO_PROTOCOL_GUID;
static EFI_GUID simple_fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID file_info_guid = EFI_FILE_INFO_ID;

static inline void dbg_putc(char c) {
    __asm__ __volatile__("outb %0, $0xe9" : : "a"(c));
}

static UINT64 align_down(UINT64 v, UINT64 a) {
    return v & ~(a - 1);
}

static UINT64 align_up(UINT64 v, UINT64 a) {
    return (v + a - 1) & ~(a - 1);
}

static UINTN str16_len(const CHAR16* s) {
    UINTN n = 0;
    while (s[n] != 0) {
        n++;
    }
    return n;
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

static CHAR16 char16_to_lower(CHAR16 c) {
    if (c >= L'A' && c <= L'Z') {
        return (CHAR16)(c - L'A' + L'a');
    }
    return c;
}

static int str16_eq_ci(const CHAR16* a, const CHAR16* b) {
    UINTN i = 0;
    while (a[i] != 0 && b[i] != 0) {
        if (char16_to_lower(a[i]) != char16_to_lower(b[i])) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static EFI_STATUS build_sibling_kernel_path(
    EFI_SYSTEM_TABLE* SystemTable,
    EFI_LOADED_IMAGE_PROTOCOL* loaded_image,
    CHAR16** kernel_path_out
) {
    if (kernel_path_out == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *kernel_path_out = NULL;

    if (loaded_image == NULL || loaded_image->FilePath == NULL) {
        return EFI_NOT_FOUND;
    }

    EFI_DEVICE_PATH_PROTOCOL* node = (EFI_DEVICE_PATH_PROTOCOL*)loaded_image->FilePath;
    while (!IsDevicePathEnd(node)) {
        if (DevicePathType(node) == MEDIA_DEVICE_PATH &&
            DevicePathSubType(node) == MEDIA_FILEPATH_DP) {
            const FILEPATH_DEVICE_PATH* fp = (const FILEPATH_DEVICE_PATH*)node;
            UINTN node_len = DevicePathNodeLength(node);
            if (node_len < SIZE_OF_FILEPATH_DEVICE_PATH + sizeof(CHAR16)) {
                return EFI_LOAD_ERROR;
            }

            UINTN max_chars = (node_len - SIZE_OF_FILEPATH_DEVICE_PATH) / sizeof(CHAR16);
            UINTN path_len = 0;
            while (path_len < max_chars && fp->PathName[path_len] != 0) {
                path_len++;
            }
            if (path_len == 0) {
                return EFI_NOT_FOUND;
            }

            INTN last_sep = -1;
            for (UINTN i = 0; i < path_len; i++) {
                if (fp->PathName[i] == L'\\' || fp->PathName[i] == L'/') {
                    last_sep = (INTN)i;
                }
            }

            const CHAR16* kernel_name = L"kernel.elf";
            UINTN kernel_name_len = str16_len(kernel_name);
            UINTN dir_len = (last_sep >= 0) ? ((UINTN)last_sep + 1) : 0;
            UINTN need_root_prefix = (dir_len == 0) ? 1 : 0;
            UINTN out_chars = dir_len + need_root_prefix + kernel_name_len + 1;

            CHAR16* out = NULL;
            EFI_STATUS status = uefi_call_wrapper(
                SystemTable->BootServices->AllocatePool,
                3,
                EfiLoaderData,
                out_chars * sizeof(CHAR16),
                (void**)&out
            );
            if (EFI_ERROR(status) || out == NULL) {
                return status;
            }

            UINTN p = 0;
            if (need_root_prefix) {
                out[p++] = L'\\';
            } else {
                for (UINTN i = 0; i < dir_len; i++) {
                    CHAR16 c = fp->PathName[i];
                    out[p++] = (c == L'/') ? L'\\' : c;
                }
            }
            for (UINTN i = 0; i < kernel_name_len; i++) {
                out[p++] = kernel_name[i];
            }
            out[p] = 0;

            *kernel_path_out = out;
            return EFI_SUCCESS;
        }

        node = NextDevicePathNode(node);
    }

    return EFI_NOT_FOUND;
}

static EFI_STATUS read_open_file_to_buffer(
    EFI_SYSTEM_TABLE* SystemTable,
    EFI_FILE_PROTOCOL* file,
    void** kernel_buffer_out,
    UINTN* kernel_size_out
) {
    EFI_STATUS status;
    EFI_FILE_INFO* file_info = NULL;
    void* kernel_buffer = NULL;
    UINTN file_info_size = SIZE_OF_EFI_FILE_INFO + 256 * sizeof(CHAR16);

    status = uefi_call_wrapper(
        SystemTable->BootServices->AllocatePool,
        3,
        EfiLoaderData,
        file_info_size,
        (void**)&file_info
    );
    if (EFI_ERROR(status) || file_info == NULL) {
        return status;
    }

    status = uefi_call_wrapper(
        file->GetInfo,
        4,
        file,
        &file_info_guid,
        &file_info_size,
        file_info
    );
    if (status == EFI_BUFFER_TOO_SMALL && file_info_size > 0) {
        uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, file_info);
        file_info = NULL;

        status = uefi_call_wrapper(
            SystemTable->BootServices->AllocatePool,
            3,
            EfiLoaderData,
            file_info_size,
            (void**)&file_info
        );
        if (EFI_ERROR(status) || file_info == NULL) {
            return status;
        }

        status = uefi_call_wrapper(
            file->GetInfo,
            4,
            file,
            &file_info_guid,
            &file_info_size,
            file_info
        );
    }

    if (EFI_ERROR(status) || file_info->FileSize == 0 || file_info->FileSize > (UINT64)(~(UINTN)0)) {
        uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, file_info);
        return EFI_LOAD_ERROR;
    }

    UINTN file_size = (UINTN)file_info->FileSize;
    uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, file_info);

    status = uefi_call_wrapper(
        SystemTable->BootServices->AllocatePool,
        3,
        EfiLoaderData,
        file_size,
        (void**)&kernel_buffer
    );
    if (EFI_ERROR(status) || kernel_buffer == NULL) {
        return status;
    }

    status = uefi_call_wrapper(file->SetPosition, 2, file, 0);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, kernel_buffer);
        return status;
    }

    UINT8* out = (UINT8*)kernel_buffer;
    UINTN remaining = file_size;
    while (remaining > 0) {
        UINTN chunk = remaining;
        status = uefi_call_wrapper(
            file->Read,
            3,
            file,
            &chunk,
            out
        );
        if (EFI_ERROR(status) || chunk == 0) {
            uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, kernel_buffer);
            return EFI_LOAD_ERROR;
        }
        out += chunk;
        remaining -= chunk;
    }

    *kernel_buffer_out = kernel_buffer;
    *kernel_size_out = file_size;
    return EFI_SUCCESS;
}

static int elf_header_is_x86_64_kernel(const Elf64_Ehdr* ehdr) {
    if (ehdr == NULL) {
        return 0;
    }

    if (!(ehdr->e_ident[0] == 0x7F &&
          ehdr->e_ident[1] == 'E' &&
          ehdr->e_ident[2] == 'L' &&
          ehdr->e_ident[3] == 'F')) {
        return 0;
    }

    if (ehdr->e_ident[4] != ELFCLASS64) {
        return 0;
    }

    if (ehdr->e_machine != EM_X86_64) {
        return 0;
    }

    return 1;
}

static int file_is_x86_64_elf(EFI_FILE_PROTOCOL* file) {
    EFI_STATUS status;
    Elf64_Ehdr ehdr;

    status = uefi_call_wrapper(file->SetPosition, 2, file, 0);
    if (EFI_ERROR(status)) {
        return 0;
    }

    UINTN read_size = sizeof(ehdr);
    status = uefi_call_wrapper(file->Read, 3, file, &read_size, &ehdr);
    if (EFI_ERROR(status) || read_size < sizeof(ehdr)) {
        return 0;
    }

    return elf_header_is_x86_64_kernel(&ehdr);
}

static EFI_STATUS read_kernel_recursive(
    EFI_SYSTEM_TABLE* SystemTable,
    EFI_FILE_PROTOCOL* dir,
    UINTN depth,
    void** kernel_buffer_out,
    UINTN* kernel_size_out
) {
    if (depth > 8) {
        return EFI_NOT_FOUND;
    }

    EFI_STATUS status = uefi_call_wrapper(dir->SetPosition, 2, dir, 0);
    if (EFI_ERROR(status)) {
        return status;
    }

    UINTN info_buf_size = 512;
    EFI_FILE_INFO* info = NULL;
    status = uefi_call_wrapper(
        SystemTable->BootServices->AllocatePool,
        3,
        EfiLoaderData,
        info_buf_size,
        (void**)&info
    );
    if (EFI_ERROR(status) || info == NULL) {
        return status;
    }

    for (;;) {
        UINTN read_size = info_buf_size;
        status = uefi_call_wrapper(dir->Read, 3, dir, &read_size, info);
        if (status == EFI_BUFFER_TOO_SMALL) {
            UINTN new_size = (read_size > info_buf_size) ? read_size : (info_buf_size * 2);
            if (new_size < info_buf_size) {
                uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, info);
                return EFI_OUT_OF_RESOURCES;
            }
            uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, info);
            info = NULL;
            info_buf_size = new_size;
            status = uefi_call_wrapper(
                SystemTable->BootServices->AllocatePool,
                3,
                EfiLoaderData,
                info_buf_size,
                (void**)&info
            );
            if (EFI_ERROR(status) || info == NULL) {
                return EFI_OUT_OF_RESOURCES;
            }
            continue;
        }
        if (EFI_ERROR(status)) {
            uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, info);
            return status;
        }
        if (read_size == 0) {
            break;
        }

        if (info->FileName[0] == 0) {
            continue;
        }

        if (str16_eq_ci(info->FileName, L".") || str16_eq_ci(info->FileName, L"..")) {
            continue;
        }

        if ((info->Attribute & EFI_FILE_DIRECTORY) == 0) {
            EFI_FILE_PROTOCOL* file = NULL;
            status = uefi_call_wrapper(
                dir->Open,
                5,
                dir,
                &file,
                info->FileName,
                EFI_FILE_MODE_READ,
                0
            );
            if (EFI_ERROR(status) || file == NULL) {
                continue;
            }

            int accept_name = str16_eq_ci(info->FileName, L"kernel.elf");
            int accept_header = file_is_x86_64_elf(file);
            if (accept_name || accept_header) {
                status = read_open_file_to_buffer(SystemTable, file, kernel_buffer_out, kernel_size_out);
                uefi_call_wrapper(file->Close, 1, file);
                if (!EFI_ERROR(status)) {
                    uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, info);
                    return EFI_SUCCESS;
                }
            }

            uefi_call_wrapper(file->Close, 1, file);
            continue;
        }

        EFI_FILE_PROTOCOL* subdir = NULL;
        status = uefi_call_wrapper(
            dir->Open,
            5,
            dir,
            &subdir,
            info->FileName,
            EFI_FILE_MODE_READ,
            0
        );
        if (EFI_ERROR(status) || subdir == NULL) {
            continue;
        }

        status = read_kernel_recursive(SystemTable, subdir, depth + 1, kernel_buffer_out, kernel_size_out);
        uefi_call_wrapper(subdir->Close, 1, subdir);
        if (!EFI_ERROR(status)) {
            uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, info);
            return EFI_SUCCESS;
        }
    }

    uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, info);
    return EFI_NOT_FOUND;
}

static EFI_STATUS read_kernel_from_root(
    EFI_SYSTEM_TABLE* SystemTable,
    EFI_FILE_PROTOCOL* root,
    const CHAR16* preferred_path,
    void** kernel_buffer_out,
    UINTN* kernel_size_out
) {
    EFI_STATUS status;

    const CHAR16* kernel_paths[] = {
        L"\\kernel.elf",
        L"\\KERNEL.ELF",
        L"\\KERNEL~1.ELF",
        L"\\EFI\\BOOT\\kernel.elf",
        L"\\EFI\\BOOT\\KERNEL.ELF",
        L"\\EFI\\BOOT\\KERNEL~1.ELF",
        L"kernel.elf",
        L"KERNEL.ELF",
        L"KERNEL~1.ELF",
        L"EFI\\BOOT\\kernel.elf",
        L"EFI\\BOOT\\KERNEL.ELF",
        L"EFI\\BOOT\\KERNEL~1.ELF",
    };

    if (preferred_path != NULL && preferred_path[0] != 0) {
        EFI_FILE_PROTOCOL* kernel_file = NULL;
        status = uefi_call_wrapper(
            root->Open,
            5,
            root,
            &kernel_file,
            (CHAR16*)preferred_path,
            EFI_FILE_MODE_READ,
            0
        );
        if (!EFI_ERROR(status) && kernel_file != NULL) {
            status = read_open_file_to_buffer(SystemTable, kernel_file, kernel_buffer_out, kernel_size_out);
            uefi_call_wrapper(kernel_file->Close, 1, kernel_file);
            if (!EFI_ERROR(status)) {
                return EFI_SUCCESS;
            }
        }
    }

    for (UINTN i = 0; i < sizeof(kernel_paths) / sizeof(kernel_paths[0]); i++) {
        EFI_FILE_PROTOCOL* kernel_file = NULL;
        status = uefi_call_wrapper(
            root->Open,
            5,
            root,
            &kernel_file,
            (CHAR16*)kernel_paths[i],
            EFI_FILE_MODE_READ,
            0
        );
        if (EFI_ERROR(status) || kernel_file == NULL) {
            continue;
        }

        status = read_open_file_to_buffer(SystemTable, kernel_file, kernel_buffer_out, kernel_size_out);
        uefi_call_wrapper(kernel_file->Close, 1, kernel_file);
        if (!EFI_ERROR(status)) {
            return EFI_SUCCESS;
        }
    }

    return read_kernel_recursive(SystemTable, root, 0, kernel_buffer_out, kernel_size_out);
}

static EFI_STATUS load_kernel_file(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE* SystemTable,
    void** kernel_buffer_out,
    UINTN* kernel_size_out
) {
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL* loaded_image = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* simple_fs = NULL;
    EFI_FILE_PROTOCOL* root = NULL;
    CHAR16* preferred_kernel_path = NULL;
    EFI_STATUS preferred_path_status = EFI_NOT_FOUND;

    *kernel_buffer_out = NULL;
    *kernel_size_out = 0;

    dbg_putc('f');
    status = uefi_call_wrapper(
        SystemTable->BootServices->HandleProtocol,
        3,
        ImageHandle,
        &loaded_image_guid,
        (void**)&loaded_image
    );
    if (EFI_ERROR(status) || loaded_image == NULL) {
        dbg_putc('g');
    } else {
        preferred_path_status = build_sibling_kernel_path(SystemTable, loaded_image, &preferred_kernel_path);
        if (!EFI_ERROR(preferred_path_status) && preferred_kernel_path != NULL) {
            dbg_putc('h');
        }
    }

    if (!EFI_ERROR(status) && loaded_image != NULL) {
        status = uefi_call_wrapper(
            SystemTable->BootServices->HandleProtocol,
            3,
            loaded_image->DeviceHandle,
            &simple_fs_guid,
            (void**)&simple_fs
        );
        if (EFI_ERROR(status) || simple_fs == NULL) {
            dbg_putc('i');
        }
        if (!EFI_ERROR(status) && simple_fs != NULL) {
            status = uefi_call_wrapper(simple_fs->OpenVolume, 2, simple_fs, &root);
            if (EFI_ERROR(status) || root == NULL) {
                dbg_putc('j');
            }
            if (!EFI_ERROR(status) && root != NULL) {
                status = read_kernel_from_root(
                    SystemTable,
                    root,
                    preferred_kernel_path,
                    kernel_buffer_out,
                    kernel_size_out
                );
                uefi_call_wrapper(root->Close, 1, root);
                if (!EFI_ERROR(status)) {
                    if (preferred_kernel_path != NULL) {
                        uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, preferred_kernel_path);
                    }
                    dbg_putc('k');
                    return EFI_SUCCESS;
                }
            }
        }
    }

    dbg_putc('l');
    EFI_HANDLE* handles = NULL;
    UINTN handle_count = 0;
    status = uefi_call_wrapper(
        SystemTable->BootServices->LocateHandleBuffer,
        5,
        ByProtocol,
        &simple_fs_guid,
        NULL,
        &handle_count,
        &handles
    );
    if (EFI_ERROR(status) || handles == NULL || handle_count == 0) {
        if (preferred_kernel_path != NULL) {
            uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, preferred_kernel_path);
        }
        dbg_putc('m');
        return EFI_NOT_FOUND;
    }

    EFI_STATUS final_status = EFI_NOT_FOUND;
    for (UINTN i = 0; i < handle_count; i++) {
        simple_fs = NULL;
        root = NULL;

        status = uefi_call_wrapper(
            SystemTable->BootServices->HandleProtocol,
            3,
            handles[i],
            &simple_fs_guid,
            (void**)&simple_fs
        );
        if (EFI_ERROR(status) || simple_fs == NULL) {
            continue;
        }

        status = uefi_call_wrapper(simple_fs->OpenVolume, 2, simple_fs, &root);
        if (EFI_ERROR(status) || root == NULL) {
            continue;
        }

        status = read_kernel_from_root(
            SystemTable,
            root,
            preferred_kernel_path,
            kernel_buffer_out,
            kernel_size_out
        );
        uefi_call_wrapper(root->Close, 1, root);

        if (!EFI_ERROR(status)) {
            final_status = EFI_SUCCESS;
            break;
        }
    }

    if (preferred_kernel_path != NULL) {
        uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, preferred_kernel_path);
    }
    uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, handles);
    if (final_status == EFI_SUCCESS) {
        dbg_putc('o');
    } else {
        dbg_putc('p');
    }
    return final_status;
}

static EFI_STATUS load_kernel_from_buffer(
    EFI_SYSTEM_TABLE* SystemTable,
    EFI_MEMORY_DESCRIPTOR* mmap,
    UINTN mmap_size,
    UINTN desc_size,
    const void* kernel_buffer,
    UINTN kernel_size,
    kernel_entry_raw_t* entry_out
) {
    EFI_STATUS status;
    (void)mmap;
    (void)mmap_size;
    (void)desc_size;
    (void)kernel_size;

    dbg_putc('1');

    const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)kernel_buffer;

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

    const Elf64_Phdr* phdrs = (const Elf64_Phdr*)((const UINT8*)kernel_buffer + ehdr->e_phoff);

    Elf64_Addr min_vaddr = ~0ULL;
    Elf64_Addr max_vaddr = 0;

    for (Elf64_Half i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr* ph = &phdrs[i];
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

    if (image_base < KERNEL_MIN_LOAD_ADDR) {
        Print(L"kernel image base below minimum: %lx < %lx\r\n", image_base, KERNEL_MIN_LOAD_ADDR);
        return EFI_LOAD_ERROR;
    }

    /*
     * Kernel is linked as a non-relocatable ELF (absolute addresses in code/data),
     * so it must be loaded at its link-time base.
     */
    EFI_PHYSICAL_ADDRESS load_base = (EFI_PHYSICAL_ADDRESS)image_base;
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

    INT64 slide = 0;
    Print(L"kernel slide=%lx\r\n", (UINT64)slide);

    for (Elf64_Half i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr* ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;

        UINT64 dst = (UINT64)ph->p_vaddr;

        dbg_putc('5');

        uefi_call_wrapper(
            SystemTable->BootServices->CopyMem,
            3,
            (void*)(UINTN)dst,
            (const void*)((const UINT8*)kernel_buffer + ph->p_offset),
            (UINTN)ph->p_filesz
        );
    }

    *entry_out = (kernel_entry_raw_t)(UINTN)ehdr->e_entry;

    dbg_putc('6');
    return EFI_SUCCESS;
}

static EFI_STATUS load_boot_volume_image(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE* SystemTable,
    EFI_PHYSICAL_ADDRESS* image_base_out,
    UINT64* image_size_out,
    EFI_BLOCK_IO_PROTOCOL** block_io_out,
    UINT32* media_id_out,
    UINT32* block_size_out,
    UINT64* block_count_out
) {
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL* loaded_image = NULL;
    EFI_BLOCK_IO_PROTOCOL* block_io = NULL;

    *image_base_out = 0;
    *image_size_out = 0;
    *block_io_out = NULL;
    *media_id_out = 0;
    *block_size_out = 0;
    *block_count_out = 0;

    status = uefi_call_wrapper(
        SystemTable->BootServices->HandleProtocol,
        3,
        ImageHandle,
        &loaded_image_guid,
        (void**)&loaded_image
    );
    if (EFI_ERROR(status) || loaded_image == NULL) {
        return EFI_NOT_FOUND;
    }

    status = uefi_call_wrapper(
        SystemTable->BootServices->HandleProtocol,
        3,
        loaded_image->DeviceHandle,
        &block_io_guid,
        (void**)&block_io
    );
    if (EFI_ERROR(status) || block_io == NULL || block_io->Media == NULL) {
        return EFI_NOT_FOUND;
    }

    if (!block_io->Media->MediaPresent || block_io->Media->BlockSize == 0) {
        return EFI_NOT_FOUND;
    }

    UINT64 block_size = (UINT64)block_io->Media->BlockSize;
    UINT64 block_count = (UINT64)block_io->Media->LastBlock + 1ULL;
    UINT64 total_size = block_size * block_count;

    if (block_count == 0 || total_size == 0) {
        return EFI_LOAD_ERROR;
    }

    UINT64 bytes_to_copy = total_size;
    if (bytes_to_copy > BOOT_VOLUME_COPY_LIMIT) {
        bytes_to_copy = BOOT_VOLUME_COPY_LIMIT;
    }

    UINT64 blocks_to_copy = bytes_to_copy / block_size;
    if ((bytes_to_copy % block_size) != 0) {
        blocks_to_copy++;
    }
    UINT64 alloc_size = blocks_to_copy * block_size;

    EFI_PHYSICAL_ADDRESS image_base = 0;
    status = uefi_call_wrapper(
        SystemTable->BootServices->AllocatePages,
        4,
        AllocateAnyPages,
        EfiLoaderData,
        EFI_SIZE_TO_PAGES(alloc_size),
        &image_base
    );
    if (EFI_ERROR(status)) {
        return status;
    }

    UINT64 lba = 0;
    UINT64 copied = 0;
    const UINT64 chunk_blocks = 128;

    while (lba < blocks_to_copy) {
        UINT64 remaining = blocks_to_copy - lba;
        UINT64 blocks_now = (remaining < chunk_blocks) ? remaining : chunk_blocks;
        UINTN bytes_now = (UINTN)(blocks_now * block_size);

        status = uefi_call_wrapper(
            block_io->ReadBlocks,
            5,
            block_io,
            block_io->Media->MediaId,
            (EFI_LBA)lba,
            bytes_now,
            (void*)(UINTN)(image_base + copied)
        );
        if (EFI_ERROR(status)) {
            uefi_call_wrapper(
                SystemTable->BootServices->FreePages,
                2,
                image_base,
                EFI_SIZE_TO_PAGES(alloc_size)
            );
            return status;
        }

        lba += blocks_now;
        copied += blocks_now * block_size;
    }

    *image_base_out = image_base;
    *image_size_out = alloc_size;
    *block_io_out = block_io;
    *media_id_out = block_io->Media->MediaId;
    *block_size_out = block_io->Media->BlockSize;
    *block_count_out = blocks_to_copy;
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

    EFI_PHYSICAL_ADDRESS boot_disk_base = 0;
    UINT64 boot_disk_size = 0;
    EFI_BLOCK_IO_PROTOCOL* boot_block_io = NULL;
    UINT32 boot_disk_media_id = 0;
    UINT32 boot_disk_block_size = 0;
    UINT64 boot_disk_block_count = 0;
    status = load_boot_volume_image(
        ImageHandle,
        SystemTable,
        &boot_disk_base,
        &boot_disk_size,
        &boot_block_io,
        &boot_disk_media_id,
        &boot_disk_block_size,
        &boot_disk_block_count
    );
    if (EFI_ERROR(status)) {
        boot_disk_base = 0;
        boot_disk_size = 0;
        boot_block_io = NULL;
        boot_disk_media_id = 0;
        boot_disk_block_size = 0;
        boot_disk_block_count = 0;
        dbg_putc('v');
    } else {
        dbg_putc('w');
    }

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

    void* kernel_file_buffer = NULL;
    UINTN kernel_file_size = 0;
    status = load_kernel_file(
        ImageHandle,
        SystemTable,
        &kernel_file_buffer,
        &kernel_file_size
    );
    if (EFI_ERROR(status)) {
        dbg_putc('n');
        fatal(SystemTable, L"Kernel file load failed", status);
    }
    dbg_putc('N');

    kernel_entry_raw_t kernel_entry = NULL;
    status = load_kernel_from_buffer(
        SystemTable,
        mmap,
        mmap_size,
        desc_size,
        kernel_file_buffer,
        kernel_file_size,
        &kernel_entry
    );
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, kernel_file_buffer);
        fatal(SystemTable, L"Kernel load failed", status);
    }

    uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, kernel_file_buffer);

    dbg_putc('F');
    (void)map_key;
    (void)desc_version;

    boot_info->fb = *fb_ptr;
    boot_info->mmap = (uint64_t)(UINTN)mmap;
    boot_info->mmap_size = (uint64_t)mmap_size;
    boot_info->desc_size = (uint64_t)desc_size;
    boot_info->boot_disk_base = (uint64_t)boot_disk_base;
    boot_info->boot_disk_size = (uint64_t)boot_disk_size;
    boot_info->efi_reset_system = (uint64_t)(UINTN)SystemTable->RuntimeServices->ResetSystem;
    boot_info->efi_block_io = (uint64_t)(UINTN)boot_block_io;
    boot_info->boot_disk_lba_start = 0;
    boot_info->boot_disk_block_count = (uint64_t)boot_disk_block_count;
    boot_info->boot_disk_media_id = (uint32_t)boot_disk_media_id;
    boot_info->boot_disk_block_size = (uint32_t)boot_disk_block_size;
    boot_info->boot_disk_read_only = (boot_block_io && boot_block_io->Media)
        ? (uint32_t)boot_block_io->Media->ReadOnly
        : 1u;
    boot_info->boot_services_active = 1u;

    dbg_putc('L');

    kernel_entry(boot_info);

    dbg_putc('Z');

    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }

    return EFI_SUCCESS;
}
