# ABI Freeze Policy (v1.x)

Last updated: 2026-04-11

This document defines the platform-compatibility contract for MyaOS v1.x.

## Compatibility Promise

For all `1.x.y` releases:

- no breaking syscall ABI changes
- no syscall number reassignments/removals
- binaries built today must keep running on future `1.x` releases

Breaking ABI changes are allowed only in `2.0.0+`.

## What Is Frozen

Frozen interface surface for v1.x:

- syscall numbers and argument ABI in `include/myaos/syscall.h`
- user-visible structures used by syscalls
- command-resolution contract (`/cmd/*.cmd` -> `/bin/*`)
- package ABI gate behavior (`package_abi <= system_abi`)

## Change Rules in v1.x

Allowed:

- add new syscalls with new numbers only
- add new optional fields at struct tail (with reserved/size-safe behavior)
- improve diagnostics and implementation details without changing ABI semantics

Not allowed:

- renumber existing syscalls
- remove existing syscalls
- change argument meaning in incompatible way
- shrink/reorder existing ABI structs

## Syscall Stability Levels

Official syscall catalog with status labels:

- `supported`: frozen behavior expected for v1.x
- `deprecated`: kept for compatibility; use replacement API
- `unstable`: ABI number is reserved, but behavior may still evolve during v1.x hardening

See: `docs/SYSCALLS_OFFICIAL.md`.

## Deprecation Policy

If syscall is deprecated:

- it remains available for all v1.x
- replacement must be documented
- warning is documented in syscall catalog

Removal is postponed until next major version.

## Userspace Baseline (Must Not Break)

The following baseline command entrypoints are platform-critical:

- `/bin/sh`
- `/bin/ls`
- `/bin/cat`
- `/bin/pkg`

These paths are treated as compatibility contract for v1.x.

Runtime gate:

- `scripts/test_baseline.sh` validates baseline commands in QEMU.

