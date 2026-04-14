# Developer Guide

## Build

```bash
make -j4
```

Artifacts:

- `build/kernel.elf`
- `build/BOOTX64.EFI`
- `build/esp.img`

## Run

```bash
make run
```

## Debug

```bash
make debug-shell
make gdb-shell
```

## Tests

```bash
make test
make test-runtime
```

## Project Layout

- `kernel/core`: kernel entry, console, panic/log, syscall dispatch, shell
- `kernel/arch/x86_64`: low-level CPU/interrupt code
- `kernel/arch/portable`: portable profile stubs (second arch profile)
- `kernel/mm`: PMM, heap, paging and address spaces
- `kernel/proc`: scheduler, ELF loader, process state
- `kernel/fs`: VFS and filesystem backends
- `kernel/dev`: device model and drivers
- `kernel/net`: loopback stack and sockets
- `programs/`: user-space commands
- `include/myaos/syscall.h`: stable user/kernel ABI

## Adding a Syscall

1. Add syscall number and ABI structs/constants in `include/myaos/syscall.h`.
2. Add user wrapper in `programs/lib/myaos.h`.
3. Implement handling in `kernel/core/syscall.c`.
4. Implement subsystem logic in `kernel/*`.
5. Rebuild with `make -j4`.

## Coding Rules

- Keep kernel/user boundary strict: no kernel-private headers in user programs.
- Validate user pointers in syscall entry.
- Prefer small, explicit interfaces between subsystems.

## Packaging

- Package format spec: `docs/PACKAGES.md`
- Host-side package builder: `tools/mkpkg.sh`
- In-system package manager: `/bin/pkg.elf` (`pkg` command)

## Dynamic Loader and Shared Libraries

- User-space dynamic loader helpers: `programs/lib/dynload.h`
- Demo shared library source: `programs/shared/libdemo.c`
- Relocation/extern-symbol demo shared library: `programs/shared/liblinkdemo.c`
- Built shared object paths in image: `/lib/libdemo.so`, `/lib/liblinkdemo.so`
- Validation command: `dlcheck`

## Modules and Optional Components

- Kernel module registry: `kernel/core/module.c`
- Runtime management command: `modctl`
- Optional components currently toggled as modules: `swap`, `hotplug`, `ldso`, `posix`
- Module details: `docs/MODULES.md`

## Swap and Hot-Plug

- Swap subsystem: `kernel/mm/swap.c`
- Explicit user APIs: `mya_mem_swap_out`, `mya_mem_swap_in`, `mya_swap_info`
- Runtime RAM disk hot-plug API: `mya_hotplug_ramdisk`

## Portability Profiles

- Build profile selector: `ARCH` Make variable (`x86_64`, `portable`)
- Profile details: `docs/PORTABILITY.md`
