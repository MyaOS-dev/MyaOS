# Backward Compatibility Policy

## ABI Versioning

System call ABI is versioned in `include/myaos/syscall.h` using:

- `MYAOS_ABI_VERSION_MAJOR`
- `MYAOS_ABI_VERSION_MINOR`
- `MYAOS_ABI_VERSION_PATCH`
- `MYAOS_ABI_VERSION`

Authoritative freeze policy for v1.x is documented in `docs/ABI.md`.

Policy:

- Patch/minor updates must remain backward-compatible for existing user programs.
- Major updates may break compatibility and require program/package rebuild.

## Package Compatibility Gate

Package metadata includes numeric `abi` field (`MYAPKG1` format).

- Package manager (`pkg`) rejects installation if `package_abi > system_abi`.
- Repository install/upgrade (`pkg install-from`, `pkg upgrade`, `update`) only select entries with compatible ABI.

## Upgrade Safety

- Installed package database (`/var/pkg/installed.db`) is updated only after package payload write and metadata write succeed.
- Failed package install does not mark package as upgraded.

## Interface Stability Scope

This policy currently covers:

- syscall/user ABI
- package ABI gating
- repository upgrade selection

Related status docs:

- Linux userspace compatibility matrix: `docs/LINUX_COMPAT_MATRIX.md`
- subsystem rollout status: `docs/SUBSYSTEM_STATUS.md`

Future work (not yet implemented):

- dynamic linker ABI compatibility contracts
- detailed kernel module ABI compatibility policy (version negotiation beyond `MYAOS_KMOD_ABI_VERSION`)

## Legacy Compatibility Layers

- `compat` command is migration-only and should not receive new feature work.
- Unix-style alias command names are transitional and should not replace native naming in new tooling/docs.
