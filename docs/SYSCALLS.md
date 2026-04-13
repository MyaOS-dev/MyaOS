# Syscalls

The stable syscall ABI is defined in `include/myaos/syscall.h`.
ABI version macros are exported as `MYAOS_ABI_VERSION_*`.

For status tracking of each official syscall (`supported`/`deprecated`/`unstable`),
see `docs/SYSCALLS_OFFICIAL.md`.

## Groups

- console: write text, clear screen, read one input character (polling)
- filesystem: list entries, read files, save files, create directory nodes, create file nodes, remove nodes, change/get current path, list attachments, attach new filesystems, chmod/chown ACL metadata
- processes: spawn, exec, wait, wait-poll, kill, exit, yield, pause, getpid, list processes, scheduler info, per-process limits (set/get)
- process messaging/notifications: notify, notify-poll, notify-wait (blocking + timeout), message send, message recv, pipe create/read/write/close, named shared-memory segments
- threads: create user threads inside current process address space, join/join-poll by TID
- user memory: map/unmap anonymous user pages (`mmap`-like API), explicit swap out/in, swap stats
- POSIX-fd (bootstrap): open/close/read/write/lseek/fstat/dup2/poll over regular files
- Linux compatibility mode (spawn flag `MYAOS_SPAWN_LINUX`): x86_64 Linux syscall translation for ELF64 user binaries (`PT_INTERP` load path + richer `auxv`, file/pipe/dir fd model, `getdents64`, `clone`+TLS base path, `execve/execveat`, `statx`, `prctl(PR_SET_NAME/PR_GET_NAME)`, mmap/brk, pread/pwrite, poll/fcntl, futex, rlimit, basic process/uid APIs)
- security: whoami/login
- networking: UDP/TCP sockets (open/bind/listen/accept/connect/send/recv), loopback stack stats, external UDP/IPv4 transmit syscall via active `netnic` backend (e1000/rtl8139/virtio-net)
- devices: enumerate registered devices and block disks, runtime RAM-disk hot-plug
- modules: list/load/unload kernel modules (load/unload execute module hooks)
- system: memory info, stop, restart, poweroff

## ABI Rule

Programs under `programs/` should depend on the shared ABI only.

They should not include kernel-private headers from `kernel/`.

## Current Limitation

- dynamic linker is user-space (`programs/lib/dynload.h`) and supports ELF64 `ET_DYN` with `RELA/JMPREL` relocation flow and host-symbol resolver callbacks
- full Linux desktop stack support (DRM/KMS + evdev + Xorg contract) is out of current scope; see `docs/LINUX_COMPAT_MATRIX.md`
