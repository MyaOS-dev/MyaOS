# Linux Compatibility Matrix

Last updated: 2026-04-10

This matrix describes Linux userspace compatibility through `lxrun` (x86_64 ELF path), not native MyaOS APIs.

Status legend:
- `yes`: implemented and used in practice
- `partial`: implemented subset/approximation
- `no`: currently not implemented

| Area | Status | Notes |
|---|---|---|
| ELF64 load (`PT_INTERP`, auxv basics) | yes | Linux-mode spawn path active. |
| File fd syscalls (`open/read/write/lseek`) | yes | Includes file/pipe/dir fd model. |
| `readv/writev`, `pread/pwrite` | yes | Basic behavior present. |
| `poll/ppoll/select/pselect6`, basic `fcntl` | partial | Common readiness probes covered; `F_DUPFD*` path works for file/pipe fd spaces; advanced flags remain incomplete. |
| `ioctl` terminal basics (`TCGETS`, `TIOCGWINSZ`, etc.) | partial | Minimal tty profile, enough for many CLI probes. |
| VT/KD ioctls (`VT_*`, `KD*`) | partial | Minimal state and mode handling for compatibility. |
| `clone` + TLS base path | partial | Base thread path available; full pthread semantics not guaranteed. |
| `execve`, `execveat` | yes | Supported in translation layer. |
| `statx` | partial | Core metadata fields mapped. |
| `prctl` (`PR_SET_NAME`/`PR_GET_NAME`) | yes | Limited scope. |
| `mmap/munmap/brk` | partial | Works for major use-cases; not full Linux VM contract. |
| `futex` | partial | Basic commands implemented. |
| rlimit/get*id/process basics | partial | Common APIs present. |
| Signals (full Linux semantics) | no | Only limited stubs/paths. |
| `/proc` Linux semantics | no | MyaOS has own `/proc` model. |
| DRM/KMS (`/dev/dri`) | no | Required for real Xorg/Wayland stacks, not implemented. |
| evdev/input Linux device model | no | Required for full desktop stack, not implemented. |
| Full Xorg stack | no | Blocked by missing DRM/input/device ABI surface. |

## Practical Scope Today

- Target: CLI Linux binaries and BusyBox-style tooling via `lxrun`.
- Non-target (for now): full desktop Linux graphics stack (`Xorg`, `Wayland`, `mesa` expectations).
