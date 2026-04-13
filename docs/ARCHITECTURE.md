# Architecture

MyaOS is an independent kernel/userspace design (not Unix-based).  
Unix-like command names (`ls`, `cat`, `sh`, etc.) are legacy compatibility aliases over MyaOS-native syscalls and tools.

## Kernel Layout

- `kernel/arch/x86_64`: interrupt stubs, IDT setup, low-level entry
- `kernel/arch/portable`: secondary architecture profile stubs
- `kernel/core`: boot info, console, shell, syscall dispatch, kernel entry
- `kernel/mm`: PMM, heap allocator, paging, explicit swap pool
- `kernel/fs`: FAT32, ext2/3/4, RAMFS, VFS, and filesystem driver/probe logic
- `kernel/dev`: device registry plus timer, keyboard, block I/O, and power drivers
- `kernel/core/module.*`: runtime kernel module registry for optional components
- `kernel/modules`: loadable kernel module implementations (embedded descriptors + load/unload hooks)
- `kernel/proc`: process-aware scheduler and ELF loader
- `kernel/net`: loopback IP/UDP-like stack and socket table
- `kernel/gfx`: framebuffer graphics and font code

## Execution Flow

1. UEFI boot code loads `kernel.elf` and provides boot info.
2. Kernel initializes memory management.
3. Device and console layers are initialized.
4. Block I/O enumerates copied EFI disks and VFS mounts MyaFS at `/` (and `/mya` compatibility), FAT32 boot media at `/boot`, and RAMFS at `/ram`.
5. Syscalls, interrupts, timer, and the process scheduler are brought up.
6. The shell process starts and loads external commands from `/cmd` manifests and `/bin` binaries (both with `/boot` fallbacks).

## Program Model

Programs are freestanding ELF64 binaries linked with `programs/program.ld`.

- external programs are spawned as ring 3 tasks
- kernel services stay in ring 0 and are reached via `int 0x80` syscalls
- tasks enter through `program_main(int argc, char** argv)`
- command discovery is data-driven through `/cmd/*.cmd`
- kernel state is exposed through VFS mounts like `/dvc`/`/dev` and `/prc`/`/proc`
- dynamic shared objects are loaded in user space via `programs/lib/dynload.h` (example: `/lib/libdemo.so`)
