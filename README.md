# MyaOS

MyaOS is an experimental x86_64 OS with a native syscall ABI, manifest-driven command model, package manager, kernel modularity, and Linux userspace compatibility path via `lxrun`.

## Product Direction (Kept)

The following are first-class and intentionally preserved:

- stable syscall ABI (`include/myaos/syscall.h`)
- ABI freeze policy for v1.x (`docs/ABI.md`)
- `/cmd/*.cmd` command manifests
- `lxrun` Linux ELF execution path
- native MyaOS userspace commands and libraries
- package system (`pkg`, repository index, ABI gate)
- modular kernel + loadable module hooks
- baseline CLI contract (`/bin/sh`, `/bin/ls`, `/bin/cat`, `/bin/pkg`)

## Legacy / Transitional Layers

The following are legacy and should not be expanded:

- `compat` command (`/bin/compat.elf`) is now a deprecated shim
- duplicate alias naming layers (`bash`, `halt`, `unxz`) are kept only for transition
- Unix-like aliases remain for convenience, but native command names are preferred

## Current Status

- subsystem rollout status: `docs/SUBSYSTEM_STATUS.md`
- Linux compatibility matrix: `docs/LINUX_COMPAT_MATRIX.md`
- troubleshooting and diagnostics workflow: `docs/TROUBLESHOOTING.md` and `docs/DIAGNOSTICS.md`

## Build

```bash
make
```

Run:

```bash
make run
```

Debug boot:

```bash
make debug
```

## Tests

Smoke:

```bash
./scripts/test_smoke.sh
```

Runtime regression (QEMU):

```bash
./scripts/test_runtime.sh
```

Extended regression gate:

```bash
./scripts/test_regression.sh
```

Baseline compatibility gate:

```bash
./scripts/test_baseline.sh
```

Stress suite (QEMU):

```bash
./scripts/test_stress.sh
```

## Docs Index

- `docs/ARCHITECTURE.md`
- `docs/ABI.md`
- `docs/SYSCALLS.md`
- `docs/SYSCALLS_OFFICIAL.md`
- `docs/COMPATIBILITY.md`
- `docs/SUBSYSTEM_STATUS.md`
- `docs/LINUX_COMPAT_MATRIX.md`
- `docs/DIAGNOSTICS.md`
- `docs/USER.md`
- `docs/PACKAGES.md`
- `docs/MODULES.md`
