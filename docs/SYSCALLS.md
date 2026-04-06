# Syscalls

The stable syscall ABI is defined in `include/myaos/syscall.h`.
ABI version macros are exported as `MYAOS_ABI_VERSION_*`.

## Groups

- console: write text, clear screen, read one input character (polling)
- filesystem: list entries, read files, save files, create directory nodes, create file nodes, remove nodes, change/get current path, list attachments, attach new filesystems
- processes: spawn, exec, wait, wait-poll, kill, exit, yield, pause, getpid, list processes, scheduler info, per-process limits (set/get)
- process messaging/notifications: notify, notify-poll, notify-wait (blocking + timeout), message send, message recv, pipe create/read/write/close, named shared-memory segments
- threads: create user threads inside current process address space, join/join-poll by TID
- user memory: map/unmap anonymous user pages (`mmap`-like API), explicit swap out/in, swap stats
- security: whoami/login
- networking: UDP/TCP sockets (open/bind/listen/accept/connect/send/recv), loopback stack stats, external UDP/IPv4 transmit syscall via e1000 path
- devices: enumerate registered devices and block disks, runtime RAM-disk hot-plug
- modules: list/load/unload kernel modules
- system: memory info, stop, restart, poweroff

## ABI Rule

Programs under `programs/` should depend on the shared ABI only.

They should not include kernel-private headers from `kernel/`.

## Current Limitation

- dynamic loader is currently user-space (`programs/lib/dynload.h`) and handles a minimal ELF subset
