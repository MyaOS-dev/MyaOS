# Kernel Modules

MyaOS provides a runtime kernel module registry with load/unload state.

## Current modules

- `core` (required)
- `net` (required)
- `swap` (optional)
- `hotplug` (optional)
- `ldso` (optional)
- `posix` (optional)

## User command

- `modctl` list modules
- `modctl load <name>`
- `modctl unload <name>`

## Syscalls

- `MYAOS_SYS_MOD_LIST`
- `MYAOS_SYS_MOD_LOAD`
- `MYAOS_SYS_MOD_UNLOAD`

Unloading required modules is rejected.
