# KMOD SDK Quickstart

This is a quick guide for writing kernel modules in MyaOS.

## 1. Create source file

Place module source in:

- `kernel/modules/<your_module>.c`

The build system links all `kernel/modules/*.c` files into kernel.

## 2. Include module ABI

Use:

- `#include <myaos/kmod.h>`

## 3. Add hooks

Implement:

- `int on_load(const myaos_kmod_api_t* api)`
- `int on_unload(const myaos_kmod_api_t* api)`

## 4. Export descriptor

Use `MYAOS_KMOD_DECLARE(...)` with:

- `abi_version = MYAOS_KMOD_ABI_VERSION`
- unique `name`
- optional `description`
- `optional` and `autoload`

## 5. NIC driver modules (optional)

For NIC modules:

1. Fill `myaos_kmod_netnic_driver_t`
2. Call `api->register_netnic_driver(&drv)` in `on_load`
3. Call `api->unregister_netnic_driver("driver.name")` in `on_unload`

If needed, trigger reprobe:

- `api->net_reprobe()`

## 6. Control from userspace

- `modctl` to list
- `modctl load <name>`
- `modctl unload <name>`
