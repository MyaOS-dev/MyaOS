#include <efi.h>
#include <efilib.h>
#include <efidevp.h>
#include "../kernel/core/boot.h"

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
#define BOOT_MENU_TIMEOUT_MS 3000u
#define BOOT_MENU_POLL_MS 25u

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

typedef enum {
    BOOT_KERNEL_NORMAL = 0,
    BOOT_KERNEL_PREVIOUS = 1,
} boot_kernel_choice_t;

static EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID block_io_guid = EFI_BLOCK_IO_PROTOCOL_GUID;
static EFI_GUID simple_fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID file_info_guid = EFI_FILE_INFO_ID;
static EFI_GUID gop_pref_var_guid = { 0x3f9f2e7a, 0x5f19, 0x4d7a, { 0xa6, 0x6c, 0x61, 0x98, 0x59, 0x2d, 0xb2, 0x41 } };
static EFI_GUID acpi20_guid = ACPI_20_TABLE_GUID;
static EFI_GUID acpi_guid = ACPI_TABLE_GUID;
static CHAR16 gop_pref_var_name[] = L"MyaOSGopMode";

static EFI_STATUS read_open_file_to_buffer(
    EFI_SYSTEM_TABLE* SystemTable,
    EFI_FILE_PROTOCOL* file,
    void** kernel_buffer_out,
    UINTN* kernel_size_out
);

typedef struct {
    uint8_t has_mode;
    uint32_t mode;
} gop_mode_pref_t;

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

static int guid_eq(const EFI_GUID* a, const EFI_GUID* b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    if (a->Data1 != b->Data1 || a->Data2 != b->Data2 || a->Data3 != b->Data3) {
        return 0;
    }
    for (UINTN i = 0; i < 8u; i++) {
        if (a->Data4[i] != b->Data4[i]) {
            return 0;
        }
    }
    return 1;
}

static void find_acpi_rsdp(EFI_SYSTEM_TABLE* SystemTable, uint64_t* out_rsdp, uint32_t* out_revision) {
    if (out_rsdp == NULL || out_revision == NULL) {
        return;
    }
    *out_rsdp = 0;
    *out_revision = 0u;

    if (SystemTable == NULL || SystemTable->ConfigurationTable == NULL) {
        return;
    }

    for (UINTN i = 0; i < SystemTable->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE* table = &SystemTable->ConfigurationTable[i];
        if (guid_eq(&table->VendorGuid, &acpi20_guid)) {
            *out_rsdp = (uint64_t)(UINTN)table->VendorTable;
            *out_revision = 2u;
            return;
        }
    }

    for (UINTN i = 0; i < SystemTable->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE* table = &SystemTable->ConfigurationTable[i];
        if (guid_eq(&table->VendorGuid, &acpi_guid)) {
            *out_rsdp = (uint64_t)(UINTN)table->VendorTable;
            *out_revision = 1u;
            return;
        }
    }
}

static const CHAR16* efi_status_reason(EFI_STATUS status) {
    switch (status) {
    case EFI_NOT_FOUND:
        return L"object was not found on available volumes";
    case EFI_LOAD_ERROR:
        return L"loadable image format is invalid or corrupted";
    case EFI_INVALID_PARAMETER:
        return L"internal boot argument is invalid";
    case EFI_OUT_OF_RESOURCES:
        return L"not enough memory/resources in UEFI firmware";
    case EFI_ACCESS_DENIED:
        return L"access denied by firmware or media policy";
    case EFI_DEVICE_ERROR:
        return L"device reported I/O error";
    case EFI_VOLUME_CORRUPTED:
        return L"filesystem metadata on boot volume is corrupted";
    case EFI_MEDIA_CHANGED:
        return L"boot media changed during read operation";
    default:
        return L"unspecified firmware/runtime failure";
    }
}

static void fatal(EFI_SYSTEM_TABLE* SystemTable, CHAR16* msg, EFI_STATUS status) {
    Print(L"\r\nBOOT FAILURE\r\n");
    Print(L"step: %s\r\n", msg);
    Print(L"status: %r\r\n", status);
    Print(L"reason: %s\r\n", efi_status_reason(status));
    Print(L"actions:\r\n");
    Print(L"  1) Verify /kernel.elf exists on EFI partition.\r\n");
    Print(L"  2) Reboot and press P to try /kernel.prev.elf.\r\n");
    Print(L"  3) Rebuild image: make -j4 (host) and update ESP.\r\n");
    Print(L"  4) Check disk/firmware errors if status repeats.\r\n");
    dbg_putc('!');
    for (;;) {
        uefi_call_wrapper(SystemTable->BootServices->Stall, 1, 1000000);
    }
}

static boot_kernel_choice_t boot_choose_kernel(EFI_SYSTEM_TABLE* SystemTable) {
    EFI_INPUT_KEY key;
    EFI_STATUS status;
    UINTN waited_ms = 0u;

    if (SystemTable == NULL || SystemTable->ConIn == NULL) {
        return BOOT_KERNEL_NORMAL;
    }

    Print(L"\r\nBoot menu: [Enter] normal kernel, [P] previous kernel (auto in 3s)\r\n");
    Print(L"choice> ");

    for (;;) {
        status = uefi_call_wrapper(SystemTable->ConIn->ReadKeyStroke, 2, SystemTable->ConIn, &key);
        if (!EFI_ERROR(status)) {
            CHAR16 ch = char16_to_lower(key.UnicodeChar);
            if (ch == L'p') {
                Print(L"previous\r\n");
                return BOOT_KERNEL_PREVIOUS;
            }
            if (ch == L'\r' || ch == L'\n' || ch == L' ') {
                Print(L"normal\r\n");
                return BOOT_KERNEL_NORMAL;
            }
        }

        if (waited_ms >= BOOT_MENU_TIMEOUT_MS) {
            Print(L"normal\r\n");
            return BOOT_KERNEL_NORMAL;
        }

        uefi_call_wrapper(SystemTable->BootServices->Stall, 1, (UINTN)BOOT_MENU_POLL_MS * 1000u);
        waited_ms += BOOT_MENU_POLL_MS;
    }
}

static int gop_mode_is_usable(const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info) {
    if (info == NULL) {
        return 0;
    }
    if (info->HorizontalResolution == 0 || info->VerticalResolution == 0) {
        return 0;
    }
    if (info->PixelFormat == PixelBltOnly) {
        return 0;
    }
    return 1;
}

static int gop_mode_is_listable(const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info) {
    if (info == NULL) {
        return 0;
    }
    return (info->HorizontalResolution != 0 && info->VerticalResolution != 0) ? 1 : 0;
}

static int gop_format_is_preferred(UINT32 format) {
    return format == PixelBlueGreenRedReserved8BitPerColor ||
           format == PixelRedGreenBlueReserved8BitPerColor;
}

static EFI_STATUS gop_select_mode(
    EFI_SYSTEM_TABLE* SystemTable,
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop,
    boot_fb_mode_t* out_modes,
    uint32_t out_modes_cap,
    const gop_mode_pref_t* pref,
    uint32_t* out_mode_count,
    uint32_t* out_mode_total,
    uint32_t* out_selected_mode
) {
    EFI_STATUS status;
    UINT32 max_mode;
    UINT32 current_mode;
    UINT32 best_mode = 0u;
    UINT32 best_width = 0u;
    UINT32 best_height = 0u;
    UINT8 best_preferred = 0u;
    UINT64 best_area = 0u;
    UINT8 best_valid = 0u;
    UINT8 preferred_valid = 0u;
    UINT32 preferred_mode = 0u;
    uint32_t stored_count = 0u;

    if (SystemTable == NULL || gop == NULL || gop->Mode == NULL ||
        out_mode_count == NULL || out_mode_total == NULL || out_selected_mode == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    *out_mode_count = 0u;
    *out_mode_total = 0u;
    *out_selected_mode = 0u;

    max_mode = gop->Mode->MaxMode;
    current_mode = gop->Mode->Mode;
    *out_mode_total = (uint32_t)max_mode;

    for (UINT32 mode = 0; mode < max_mode; mode++) {
        UINTN info_size = 0;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info = NULL;

        status = uefi_call_wrapper(gop->QueryMode, 4, gop, mode, &info_size, &info);
        if (EFI_ERROR(status) || info == NULL) {
            continue;
        }

        if (pref != NULL && pref->has_mode && mode == pref->mode) {
            preferred_valid = 1u;
            preferred_mode = mode;
        }

        if (gop_mode_is_listable(info)) {
            if (out_modes != NULL && stored_count < out_modes_cap) {
                out_modes[stored_count].mode = (uint32_t)mode;
                out_modes[stored_count].width = (uint32_t)info->HorizontalResolution;
                out_modes[stored_count].height = (uint32_t)info->VerticalResolution;
                out_modes[stored_count].pixels_per_scanline = (uint32_t)info->PixelsPerScanLine;
                out_modes[stored_count].format = (uint32_t)info->PixelFormat;
                stored_count++;
            }
        }

        if (gop_mode_is_usable(info)) {
            UINT8 preferred = (UINT8)gop_format_is_preferred(info->PixelFormat);
            UINT64 area = (UINT64)info->HorizontalResolution * (UINT64)info->VerticalResolution;

            if (!best_valid ||
                preferred > best_preferred ||
                (preferred == best_preferred && area > best_area) ||
                (preferred == best_preferred && area == best_area && info->HorizontalResolution > best_width) ||
                (preferred == best_preferred && area == best_area && info->HorizontalResolution == best_width &&
                 info->VerticalResolution > best_height) ||
                (preferred == best_preferred && area == best_area && info->HorizontalResolution == best_width &&
                 info->VerticalResolution == best_height && mode < best_mode)) {
                best_valid = 1u;
                best_mode = mode;
                best_width = info->HorizontalResolution;
                best_height = info->VerticalResolution;
                best_preferred = preferred;
                best_area = area;
            }
        }

        uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, info);
    }

    if (stored_count == 0u && gop_mode_is_listable(gop->Mode->Info) && out_modes != NULL && out_modes_cap > 0u) {
        out_modes[0].mode = (uint32_t)current_mode;
        out_modes[0].width = (uint32_t)gop->Mode->Info->HorizontalResolution;
        out_modes[0].height = (uint32_t)gop->Mode->Info->VerticalResolution;
        out_modes[0].pixels_per_scanline = (uint32_t)gop->Mode->Info->PixelsPerScanLine;
        out_modes[0].format = (uint32_t)gop->Mode->Info->PixelFormat;
        stored_count = 1u;
    }

    *out_mode_count = stored_count;
    if (preferred_valid) {
        *out_selected_mode = preferred_mode;
    } else {
        *out_selected_mode = best_valid ? (uint32_t)best_mode : (uint32_t)current_mode;
    }

    status = uefi_call_wrapper(gop->SetMode, 2, gop, (UINT32)*out_selected_mode);
    if (EFI_ERROR(status)) {
        if (*out_selected_mode != (uint32_t)current_mode) {
            status = uefi_call_wrapper(gop->SetMode, 2, gop, current_mode);
            if (!EFI_ERROR(status)) {
                *out_selected_mode = (uint32_t)current_mode;
                return EFI_SUCCESS;
            }
        }
        return status;
    }

    return EFI_SUCCESS;
}

static uint8_t ascii_is_space(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n') ? 1u : 0u;
}

static int parse_u32_ascii(const char* text, uint32_t len, uint32_t* out_value) {
    uint64_t value = 0u;
    uint32_t i = 0u;

    if (!text || !out_value || len == 0u) {
        return -1;
    }
    while (i < len && ascii_is_space(text[i])) {
        i++;
    }
    if (i >= len) {
        return -1;
    }
    while (i < len && !ascii_is_space(text[i])) {
        if (text[i] < '0' || text[i] > '9') {
            return -1;
        }
        value = value * 10u + (uint64_t)(text[i] - '0');
        if (value > 0xFFFFFFFFull) {
            return -1;
        }
        i++;
    }
    while (i < len && ascii_is_space(text[i])) {
        i++;
    }
    if (i != len) {
        return -1;
    }
    *out_value = (uint32_t)value;
    return 0;
}

static int parse_mode_pref_from_buffer(const uint8_t* data, UINTN size, gop_mode_pref_t* out_pref) {
    UINTN pos = 0u;

    if (!data || !out_pref) {
        return -1;
    }
    out_pref->has_mode = 0u;
    out_pref->mode = 0u;

    while (pos < size) {
        UINTN line_start = pos;
        UINTN line_end = pos;

        while (line_end < size && data[line_end] != '\n' && data[line_end] != '\r') {
            line_end++;
        }
        pos = line_end;
        while (pos < size && (data[pos] == '\n' || data[pos] == '\r')) {
            pos++;
        }

        while (line_start < line_end && ascii_is_space((char)data[line_start])) {
            line_start++;
        }
        while (line_end > line_start && ascii_is_space((char)data[line_end - 1u])) {
            line_end--;
        }
        if (line_end <= line_start) {
            continue;
        }
        if (data[line_start] == '#') {
            continue;
        }

        if ((UINTN)(line_end - line_start) >= 5u &&
            data[line_start + 0u] == 'm' &&
            data[line_start + 1u] == 'o' &&
            data[line_start + 2u] == 'd' &&
            data[line_start + 3u] == 'e' &&
            data[line_start + 4u] == '=') {
            const char* value = (const char*)(data + line_start + 5u);
            uint32_t value_len = (uint32_t)(line_end - (line_start + 5u));
            uint32_t parsed_mode = 0u;

            if (value_len == 4u &&
                value[0] == 'a' && value[1] == 'u' && value[2] == 't' && value[3] == 'o') {
                out_pref->has_mode = 0u;
                out_pref->mode = 0u;
                return 0;
            }
            if (parse_u32_ascii(value, value_len, &parsed_mode) == 0) {
                out_pref->has_mode = 1u;
                out_pref->mode = parsed_mode;
                return 0;
            }
            return -1;
        }
    }

    return 0;
}

static EFI_STATUS load_gop_mode_preference_from_variable(
    EFI_SYSTEM_TABLE* SystemTable,
    gop_mode_pref_t* out_pref
) {
    EFI_STATUS status;
    UINT32 attrs = 0u;
    UINTN data_size = sizeof(UINT32);
    UINT32 value = 0u;

    if (!SystemTable || !out_pref || !SystemTable->RuntimeServices || !SystemTable->RuntimeServices->GetVariable) {
        return EFI_UNSUPPORTED;
    }

    status = uefi_call_wrapper(
        SystemTable->RuntimeServices->GetVariable,
        5,
        gop_pref_var_name,
        &gop_pref_var_guid,
        &attrs,
        &data_size,
        &value
    );
    if (EFI_ERROR(status)) {
        return status;
    }
    if (data_size != sizeof(UINT32)) {
        return EFI_COMPROMISED_DATA;
    }

    if (value == 0xFFFFFFFFu) {
        out_pref->has_mode = 0u;
        out_pref->mode = 0u;
    } else {
        out_pref->has_mode = 1u;
        out_pref->mode = value;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS load_gop_mode_preference(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE* SystemTable,
    gop_mode_pref_t* out_pref
) {
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL* loaded_image = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* simple_fs = NULL;
    EFI_FILE_PROTOCOL* root = NULL;
    const CHAR16* pref_paths[] = {
        L"\\myares.cfg",
        L"\\EFI\\BOOT\\myares.cfg",
        L"\\myaos.resolution.cfg",
        L"\\EFI\\BOOT\\myaos.resolution.cfg",
    };

    if (!out_pref) {
        return EFI_INVALID_PARAMETER;
    }
    out_pref->has_mode = 0u;
    out_pref->mode = 0u;

    status = load_gop_mode_preference_from_variable(SystemTable, out_pref);
    if (!EFI_ERROR(status)) {
        return EFI_SUCCESS;
    }

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
        &simple_fs_guid,
        (void**)&simple_fs
    );
    if (EFI_ERROR(status) || simple_fs == NULL) {
        return EFI_NOT_FOUND;
    }

    status = uefi_call_wrapper(simple_fs->OpenVolume, 2, simple_fs, &root);
    if (EFI_ERROR(status) || root == NULL) {
        return status;
    }

    for (UINTN i = 0u; i < sizeof(pref_paths) / sizeof(pref_paths[0]); i++) {
        EFI_FILE_PROTOCOL* file = NULL;
        void* pref_buf = NULL;
        UINTN pref_size = 0u;

        status = uefi_call_wrapper(
            root->Open,
            5,
            root,
            &file,
            (CHAR16*)pref_paths[i],
            EFI_FILE_MODE_READ,
            0
        );
        if (EFI_ERROR(status) || file == NULL) {
            continue;
        }

        status = read_open_file_to_buffer(SystemTable, file, &pref_buf, &pref_size);
        uefi_call_wrapper(file->Close, 1, file);
        if (EFI_ERROR(status) || pref_buf == NULL || pref_size == 0u) {
            continue;
        }

        if (parse_mode_pref_from_buffer((const uint8_t*)pref_buf, pref_size, out_pref) == 0) {
            uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, pref_buf);
            uefi_call_wrapper(root->Close, 1, root);
            return EFI_SUCCESS;
        }

        uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, pref_buf);
    }

    uefi_call_wrapper(root->Close, 1, root);
    return EFI_NOT_FOUND;
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

static EFI_STATUS exit_boot_services_with_retry(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE* SystemTable,
    EFI_MEMORY_DESCRIPTOR** mmap_out,
    UINTN* mmap_size_out,
    UINTN* desc_size_out
) {
    EFI_STATUS status = EFI_ABORTED;
    EFI_MEMORY_DESCRIPTOR* mmap = NULL;
    UINTN mmap_size = 0;
    UINTN map_key = 0;
    UINTN desc_size = 0;
    UINT32 desc_version = 0;

    if (!mmap_out || !mmap_size_out || !desc_size_out) {
        return EFI_INVALID_PARAMETER;
    }

    *mmap_out = NULL;
    *mmap_size_out = 0;
    *desc_size_out = 0;

    for (UINTN attempt = 0; attempt < 8u; attempt++) {
        if (mmap != NULL) {
            uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, mmap);
            mmap = NULL;
        }

        status = get_memory_map_alloc(
            SystemTable,
            &mmap,
            &mmap_size,
            &map_key,
            &desc_size,
            &desc_version
        );
        if (EFI_ERROR(status)) {
            return status;
        }

        status = uefi_call_wrapper(SystemTable->BootServices->ExitBootServices, 2, ImageHandle, map_key);
        if (!EFI_ERROR(status)) {
            *mmap_out = mmap;
            *mmap_size_out = mmap_size;
            *desc_size_out = desc_size;
            return EFI_SUCCESS;
        }

        if (status != EFI_INVALID_PARAMETER) {
            uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, mmap);
            return status;
        }
    }

    if (mmap != NULL) {
        uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, mmap);
    }
    return status;
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

static EFI_STATUS read_previous_kernel_from_root(
    EFI_SYSTEM_TABLE* SystemTable,
    EFI_FILE_PROTOCOL* root,
    void** kernel_buffer_out,
    UINTN* kernel_size_out
) {
    EFI_STATUS status;
    const CHAR16* prev_paths[] = {
        L"\\kernel.prev.elf",
        L"\\KERNEL.PREV.ELF",
        L"\\KERNELPR.ELF",
        L"\\EFI\\BOOT\\kernel.prev.elf",
        L"\\EFI\\BOOT\\KERNEL.PREV.ELF",
        L"kernel.prev.elf",
        L"KERNEL.PREV.ELF",
        L"KERNELPR.ELF",
        L"EFI\\BOOT\\kernel.prev.elf",
        L"EFI\\BOOT\\KERNEL.PREV.ELF",
    };

    for (UINTN i = 0; i < sizeof(prev_paths) / sizeof(prev_paths[0]); i++) {
        EFI_FILE_PROTOCOL* kernel_file = NULL;
        status = uefi_call_wrapper(
            root->Open,
            5,
            root,
            &kernel_file,
            (CHAR16*)prev_paths[i],
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

    return EFI_NOT_FOUND;
}

static EFI_STATUS load_kernel_file(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE* SystemTable,
    boot_kernel_choice_t kernel_choice,
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
                if (kernel_choice == BOOT_KERNEL_PREVIOUS) {
                    status = read_previous_kernel_from_root(SystemTable, root, kernel_buffer_out, kernel_size_out);
                    if (EFI_ERROR(status)) {
                        Print(L"previous kernel not found on boot volume, trying normal kernel\r\n");
                    }
                }
                if (EFI_ERROR(status) || kernel_choice == BOOT_KERNEL_NORMAL) {
                    status = read_kernel_from_root(
                        SystemTable,
                        root,
                        preferred_kernel_path,
                        kernel_buffer_out,
                        kernel_size_out
                    );
                }
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

        if (kernel_choice == BOOT_KERNEL_PREVIOUS) {
            status = read_previous_kernel_from_root(SystemTable, root, kernel_buffer_out, kernel_size_out);
            if (EFI_ERROR(status)) {
                status = read_kernel_from_root(
                    SystemTable,
                    root,
                    preferred_kernel_path,
                    kernel_buffer_out,
                    kernel_size_out
                );
            }
        } else {
            status = read_kernel_from_root(
                SystemTable,
                root,
                preferred_kernel_path,
                kernel_buffer_out,
                kernel_size_out
            );
        }
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
    const void* kernel_buffer,
    UINTN kernel_size,
    kernel_entry_raw_t* entry_out
) {
    EFI_STATUS status;
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

static EFI_STATUS load_block_device_image_from_handle(
    EFI_HANDLE DeviceHandle,
    EFI_SYSTEM_TABLE* SystemTable,
    EFI_PHYSICAL_ADDRESS* image_base_out,
    UINT64* image_size_out,
    EFI_BLOCK_IO_PROTOCOL** block_io_out,
    UINT32* media_id_out,
    UINT32* block_size_out,
    UINT64* block_count_out
) {
    EFI_STATUS status;
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
        DeviceHandle,
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

static void store_boot_disk_info(
    boot_disk_info_t* out,
    EFI_PHYSICAL_ADDRESS image_base,
    UINT64 image_size,
    EFI_BLOCK_IO_PROTOCOL* block_io,
    UINT32 media_id,
    UINT32 block_size,
    UINT64 block_count,
    UINT32 is_boot
) {
    if (!out) {
        return;
    }

    out->image_base = (uint64_t)image_base;
    out->image_size = image_size;
    out->efi_block_io = (uint64_t)(UINTN)block_io;
    out->lba_start = 0;
    out->block_count = block_count;
    out->media_id = media_id;
    out->block_size = block_size;
    out->read_only = (block_io && block_io->Media) ? (uint32_t)block_io->Media->ReadOnly : 1u;
    out->is_boot = is_boot;
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

    return load_block_device_image_from_handle(
        loaded_image->DeviceHandle,
        SystemTable,
        image_base_out,
        image_size_out,
        block_io_out,
        media_id_out,
        block_size_out,
        block_count_out
    );
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    dbg_putc('A');

    EFI_STATUS status;
    uint64_t acpi_rsdp = 0;
    uint32_t acpi_revision = 0u;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;
    boot_fb_mode_t fb_modes[BOOT_MAX_FB_MODES];
    uint32_t fb_mode_count = 0u;
    uint32_t fb_mode_total = 0u;
    uint32_t fb_mode = 0u;
    gop_mode_pref_t fb_pref;

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

    (void)load_gop_mode_preference(ImageHandle, SystemTable, &fb_pref);

    status = gop_select_mode(
        SystemTable,
        gop,
        fb_modes,
        BOOT_MAX_FB_MODES,
        &fb_pref,
        &fb_mode_count,
        &fb_mode_total,
        &fb_mode
    );
    if (EFI_ERROR(status)) {
        fatal(SystemTable, L"GOP mode setup failed", status);
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
    uefi_call_wrapper(SystemTable->BootServices->SetMem, 3, boot_info, sizeof(boot_info_t), 0);
    find_acpi_rsdp(SystemTable, &acpi_rsdp, &acpi_revision);

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
        store_boot_disk_info(
            &boot_info->disks[0],
            boot_disk_base,
            boot_disk_size,
            boot_block_io,
            boot_disk_media_id,
            boot_disk_block_size,
            boot_disk_block_count,
            1u
        );
        boot_info->disk_count = 1u;
    }

    EFI_LOADED_IMAGE_PROTOCOL* loaded_image = NULL;
    EFI_HANDLE boot_device_handle = NULL;
    status = uefi_call_wrapper(
        SystemTable->BootServices->HandleProtocol,
        3,
        ImageHandle,
        &loaded_image_guid,
        (void**)&loaded_image
    );
    if (!EFI_ERROR(status) && loaded_image != NULL) {
        boot_device_handle = loaded_image->DeviceHandle;
    }

    EFI_HANDLE* block_handles = NULL;
    UINTN block_handle_count = 0;
    status = uefi_call_wrapper(
        SystemTable->BootServices->LocateHandleBuffer,
        5,
        ByProtocol,
        &block_io_guid,
        NULL,
        &block_handle_count,
        &block_handles
    );
    if (!EFI_ERROR(status) && block_handles != NULL) {
        for (UINTN i = 0; i < block_handle_count && boot_info->disk_count < BOOT_MAX_DISKS; i++) {
            EFI_PHYSICAL_ADDRESS disk_base = 0;
            UINT64 disk_size = 0;
            EFI_BLOCK_IO_PROTOCOL* disk_block_io = NULL;
            UINT32 disk_media_id = 0;
            UINT32 disk_block_size = 0;
            UINT64 disk_block_count = 0;

            if (boot_device_handle != NULL && block_handles[i] == boot_device_handle) {
                continue;
            }

            status = load_block_device_image_from_handle(
                block_handles[i],
                SystemTable,
                &disk_base,
                &disk_size,
                &disk_block_io,
                &disk_media_id,
                &disk_block_size,
                &disk_block_count
            );
            if (EFI_ERROR(status)) {
                continue;
            }

            store_boot_disk_info(
                &boot_info->disks[boot_info->disk_count],
                disk_base,
                disk_size,
                disk_block_io,
                disk_media_id,
                disk_block_size,
                disk_block_count,
                0u
            );
            boot_info->disk_count++;
        }
        uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, block_handles);
    }

    void* kernel_file_buffer = NULL;
    UINTN kernel_file_size = 0;
    boot_kernel_choice_t kernel_choice = BOOT_KERNEL_NORMAL;
    kernel_choice = boot_choose_kernel(SystemTable);
    status = load_kernel_file(
        ImageHandle,
        SystemTable,
        kernel_choice,
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
        kernel_file_buffer,
        kernel_file_size,
        &kernel_entry
    );
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, kernel_file_buffer);
        fatal(SystemTable, L"Kernel load failed", status);
    }

    uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, kernel_file_buffer);

    EFI_MEMORY_DESCRIPTOR* final_mmap = NULL;
    UINTN final_mmap_size = 0;
    UINTN final_desc_size = 0;
    status = exit_boot_services_with_retry(
        ImageHandle,
        SystemTable,
        &final_mmap,
        &final_mmap_size,
        &final_desc_size
    );
    if (EFI_ERROR(status)) {
        fatal(SystemTable, L"ExitBootServices failed", status);
    }

    dbg_putc('F');

    boot_info->fb = *fb_ptr;
    boot_info->mmap = (uint64_t)(UINTN)final_mmap;
    boot_info->mmap_size = (uint64_t)final_mmap_size;
    boot_info->desc_size = (uint64_t)final_desc_size;
    boot_info->boot_disk_base = (uint64_t)boot_disk_base;
    boot_info->boot_disk_size = (uint64_t)boot_disk_size;
    boot_info->efi_reset_system = (uint64_t)(UINTN)(
        SystemTable->RuntimeServices ? SystemTable->RuntimeServices->ResetSystem : NULL
    );
    boot_info->efi_get_variable = (uint64_t)(UINTN)(
        SystemTable->RuntimeServices ? SystemTable->RuntimeServices->GetVariable : NULL
    );
    boot_info->efi_set_variable = (uint64_t)(UINTN)(
        SystemTable->RuntimeServices ? SystemTable->RuntimeServices->SetVariable : NULL
    );
    boot_info->acpi_rsdp = acpi_rsdp;
    boot_info->efi_block_io = (uint64_t)(UINTN)boot_block_io;
    boot_info->boot_disk_lba_start = 0;
    boot_info->boot_disk_block_count = (uint64_t)boot_disk_block_count;
    boot_info->boot_disk_media_id = (uint32_t)boot_disk_media_id;
    boot_info->boot_disk_block_size = (uint32_t)boot_disk_block_size;
    boot_info->boot_disk_read_only = (boot_block_io && boot_block_io->Media)
        ? (uint32_t)boot_block_io->Media->ReadOnly
        : 1u;
    boot_info->boot_services_active = 0u;
    boot_info->fb_mode = fb_mode;
    boot_info->acpi_revision = acpi_revision;
    boot_info->fb_mode_count = fb_mode_count;
    boot_info->fb_mode_total = fb_mode_total;
    for (uint32_t i = 0u; i < fb_mode_count && i < BOOT_MAX_FB_MODES; i++) {
        boot_info->fb_modes[i] = fb_modes[i];
    }

    dbg_putc('L');

    kernel_entry(boot_info);

    dbg_putc('Z');

    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }

    return EFI_SUCCESS;
}
