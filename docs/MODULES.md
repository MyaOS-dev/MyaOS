# Kernel Modules

MyaOS now supports callback-based kernel modules with a small module ABI.

See also: [KMOD_SDK.md](KMOD_SDK.md).

## What changed

- module load/unload is no longer only a flag toggle
- modules can execute `on_load` / `on_unload` hooks
- embedded modules are discovered automatically from linker section `.myaos_kmods`
- modules can register NIC drivers through kernel module API

## Runtime commands

- `modctl` list modules
- `modctl load <name>` execute module load hook
- `modctl unload <name>` execute module unload hook

Required modules still cannot be unloaded.

## Where module sources live

- `kernel/modules/*.c`

All files from that directory are linked into kernel automatically.

## Module ABI header

- `include/myaos/kmod.h`

Main pieces:

- `myaos_kmod_descriptor_t` module metadata + hooks
- `myaos_kmod_api_t` API table passed into hooks
- `myaos_kmod_netnic_driver_t` NIC driver registration struct
- `MYAOS_KMOD_DECLARE(...)` macro to export descriptor into `.myaos_kmods`

## Example module

- `kernel/modules/rtl8125_compat.c`

It registers a compatibility NIC driver (`kmod.rtl8125`) for `10ec:8125`.

## Minimal module skeleton

```c
#include <myaos/kmod.h>

static int my_on_load(const myaos_kmod_api_t* api) {
    (void)api;
    return 0;
}

static int my_on_unload(const myaos_kmod_api_t* api) {
    (void)api;
    return 0;
}

MYAOS_KMOD_DECLARE(
    g_my_module,
    ((myaos_kmod_descriptor_t){
        .abi_version = MYAOS_KMOD_ABI_VERSION,
        .name = "my_module",
        .description = "my module",
        .optional = 1u,
        .autoload = 0u,
        .on_load = my_on_load,
        .on_unload = my_on_unload,
    })
);
```
