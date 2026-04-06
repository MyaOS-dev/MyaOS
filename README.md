# MyaOS

MyaOS is an experimental x86_64 operating system with a UEFI boot path, a modular kernel layout, a VFS layer, a syscall ABI, and loadable ELF command programs.

## Current State

The kernel now includes:

- `kernel/arch/x86_64/` for interrupt/entry code
- `kernel/arch/portable/` for secondary architecture profile stubs
- `kernel/core/` for boot data, console, shell, and syscall dispatch
- `kernel/mm/` for PMM, heap, paging, and explicit swap
- `kernel/fs/` for FAT32, RAMFS, VFS, and filesystem adapters
- `kernel/dev/` for the common device layer and device-facing drivers
- `kernel/proc/` for the scheduler/process model and ELF loader
- `kernel/gfx/` for framebuffer graphics and font rendering

The shell is intentionally small. It handles input, `help`, `cd`, command lookup from `/cmd` (with `/boot/cmd` fallback), and execution. Regular commands are built as ELF programs and loaded from `/bin` (with `/boot/bin` fallback).

## Process Model

The scheduler now tracks real process metadata:

- PID and PPID
- states: `running`, `ready`, `blocked`, `sleeping`, `zombie`
- per-process working directory
- loadable ELF entrypoints
- `spawn`, `exec`, `wait`, `exit`, `yield`, and `sleep`

External command programs now run in ring 3 with a ring 0 syscall boundary.
Kernel service tasks (shell/idle) remain ring 0.
Per-process user page tables and user memory regions are active.

## Filesystems

MyaOS now uses a VFS layer with mount points:

- `/` backed by MyaFS (standard/default filesystem)
- `/boot` backed by FAT32 boot disk when available (or demo FAT32 fallback)
- `/ram` backed by RAMFS
- `/mya` compatibility mount backed by the same MyaFS instance
- `/dvc` and `/dev` virtual device views
- `/prc` and `/proc` virtual process views
- extra disks can be enumerated and mounted later through the `attach` command

Disk probing currently recognizes:

- `fat32`
- `ext2`, `ext3`, `ext4`
- `ntfs`

Mounted filesystems currently supported through VFS:

- `fat32`
- `ext2`, `ext3`, `ext4`
- `ramfs`
- `myafs`

`myafs` is implemented in-kernel in MyaOS (`kernel/fs/vfs_myafs.c`) and mounted as the default root filesystem.

`ntfs` is still probe-only.

`ext*` write support is staged by safety level:

- level 0: read-only fallback when journal recovery is required
- level 1: limited write support (small legacy-mapped file updates)
- level 2: wider legacy block-map write support (including indirect block paths)

Generic filesystem commands now operate through the VFS instead of filesystem-specific shell logic. Current packaged commands include:

- `list`, `show`, `mkfolder`, `newfile`, `save`, `del`, `whereami`, `attached`, `attach`
- `about`, `add`, `cate`, `cls`, `say`, `pause`, `msh`, `uname`, `kill`, `compat`
- `meminfo`, `sched`, `tasks`, `devls`, `disks`, `swapstat`, `swapcheck`, `dlcheck`, `posixcheck`
- text tools: `findtext`, `count`, `firstlines`
- control/status helpers: `ok`, `fail`
- lightweight IPC tools: `notify`, `notifypoll`, `msgsend`, `msgrecv`
- `stop`, `restart`, `poweroff`, `hotplug`, `modctl`

`help` and `cd` stay built into the shell.  
Shell parsing now supports quotes/escapes, `;`, `&&`, `||`, redirection (`>`, `>>`), and a basic single-pipe flow (temp-file bridge).

## Syscall ABI

The shared ABI lives in [`include/myaos/syscall.h`](/home/timur/Desktop/myaos/include/myaos/syscall.h). It exposes:

- console syscalls
- file/VFS syscalls
- process syscalls
- module list/load/unload syscalls
- user memory map/swap syscalls
- device enumeration and hot-plug syscalls
- system and memory info syscalls

Programs under `programs/` only use that ABI through [`programs/lib/myaos.h`](/home/timur/Desktop/myaos/programs/lib/myaos.h).

## Build

Build everything:

```bash
make
```

By default this produces an optimized build (`-O2`).  
For developer diagnostics build with symbols and no optimization:

```bash
make MYAOS_DEBUG=1
```

Run in QEMU:

```bash
make run
```

Debug boot with QEMU logging:

```bash
make debug
```

Debug shell startup with symbols (QEMU waits for GDB on `:1234`):

```bash
make debug-shell
```

In another terminal:

```bash
make gdb-shell
```

`/autorun.sh` is now packaged into the ESP image and runs at shell startup (via `/boot/autorun.sh` when `/` is MyaFS).  
The default script keeps shell debug mode disabled and launches `msh`.

The Makefile now builds kernel sources from subdirectories, builds ELF programs separately, packages them into the boot image `/bin`, packages shared libraries into `/lib`, and copies command manifests into `/cmd`.

For cross-platform host tooling, see the portable MyaFS driver docs: `docs/MYAFS_DRIVER.md`.
That doc also includes the Linux host kernel filesystem module (`tools/myafs_host_linux`).

## Docs

- [Architecture](docs/ARCHITECTURE.md)
- [Syscalls](docs/SYSCALLS.md)
- [VFS](docs/VFS.md)
- [Processes](docs/PROCESSES.md)
- [MyaFS Host Driver](docs/MYAFS_DRIVER.md)
- [Packages](docs/PACKAGES.md)
- [Compatibility Policy](docs/COMPATIBILITY.md)
- [Kernel Modules](docs/MODULES.md)
- [Portability Profiles](docs/PORTABILITY.md)

## Near-Term Direction

- stronger ELF loading validation
- better process reaping and parent/child handling
- more VFS operations and filesystem drivers
- richer dynamic loader features (relocations/import resolution)
- stronger compatibility coverage for POSIX-style userspace
- more robust module lifecycle hooks for optional components
