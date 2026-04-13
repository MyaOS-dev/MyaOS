# Toolchain Bootstrap

This document tracks the staged path to an in-MyaOS C toolchain.

## Status

1. Kernel POSIX-fd base (`open/close/read/write/lseek/fstat/dup2/poll`): implemented.
2. User compatibility layer (`posix_compat.h`, `unistd.h`, `fcntl.h`, `poll.h`, `sys/stat.h`): started.
3. Compiler bootstrap: started with host-side TCC bootstrap script (`tools/toolchain/bootstrap_tcc_host.sh`).
4. In-system compiler/debug entrypoints: `/bin/cc.elf` (compiler frontend wrapper) and `/bin/dbg.elf` (debug dispatcher).
5. Full in-OS toolchain (`cc/as/ld/ar/make`, package flow): planned.

## Scope of Current POSIX-fd Base

- Regular-file descriptors only.
- Backed by VFS paths, with per-process descriptor state and seek offset.
- Poll support is basic and file-oriented.

## Next Work (Short-Term)

1. Add `isatty`, `ioctl` stubs and improve poll semantics.
2. Add fd-backed stdio (`FILE`) layer in userspace libc.
3. Bring up TCC as a hosted package and use `/bin/cc` as default frontend.
4. Add binutils subset (`as`, `ld`, `ar`) or package prebuilt tools.
