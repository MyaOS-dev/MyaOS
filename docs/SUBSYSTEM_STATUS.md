# Subsystem Status

Last updated: 2026-04-10

Status legend:
- `stable`: default path, expected for daily usage
- `beta`: feature-complete enough for active use, still hardening
- `partial`: intentionally incomplete or compatibility-only path

| Subsystem | Status | Notes |
|---|---|---|
| Boot (UEFI + GOP) | stable | Boot image + fallback behaviors in place. |
| Kernel ABI (`include/myaos/syscall.h`) | stable | ABI versioned and package-gated. |
| Command model (`/cmd` + `/bin`) | stable | Manifest-first resolution. |
| Native userspace commands | stable | Main operational surface. |
| Package manager (`pkg`) | beta | Install/upgrade/downgrade/repo flows implemented; keep expanding diagnostics. |
| Kernel modularity (`module`/`modctl`) | beta | Hook-based lifecycle works; SDK/policy still evolving. |
| VFS + MyaFS root | stable | Primary storage path. |
| FAT32/ext*/NTFS attached writes | partial | Runtime writes work in-memory; persistent flush to firmware-backed disks after `ExitBootServices` is not guaranteed yet. |
| Network stack + NIC integration | beta | Multiple drivers integrated; real-hardware coverage still grows. |
| Graphics + text console | beta | Mode switching and input path work; advanced GUI stack not complete. |
| Linux ELF execution (`lxrun`) | beta | Broad syscall coverage, not full Linux parity. |
| Linux command alias layer | partial | Legacy convenience only, avoid adding new aliases. |
| `compat` shim | partial | Deprecated transitional shim; kept for migration only. |

## Immediate Priorities

1. Keep ABI and package compatibility strict.
2. Prefer native commands/docs naming over alias naming.
3. Harden diagnostics and failure reporting around package/network/Linux-compat paths.
4. Expand regression + stress gates before adding new compatibility surface.
