# VFS

The VFS layer normalizes paths, resolves mount points, and forwards operations to mounted filesystems through a common callback table.

## Mounts

- `/` -> MyaFS (standard/default)
- `/boot` -> FAT32 boot volume or demo FAT32 image
- `/ram` -> RAMFS
- `/mya` -> MyaFS compatibility mount
- `/dvc` and `/dev` -> virtual device view
- `/prc` and `/proc` -> virtual process view
- `/cfg` and `/sys` -> virtual system configuration view
- additional disks can be mounted at arbitrary mount points with `attach diskN /path`

## Filesystem Detection

Disk probing currently identifies:

- `fat32`
- `ext2`
- `ext3`
- `ext4`
- `ntfs`

Mounted VFS backends:

- `fat32`
- `ext2`
- `ext3`
- `ext4`
- `ramfs`
- `myafs`

`ntfs` remains detection-only.

`ext*` write behavior is gated by internal safety level:

- level 0: read-only
- level 1: limited write support
- level 2: wider legacy write support

## Supported Operations

- directory listing
- file read
- file save
- create directory nodes
- create file nodes
- `remove`
- runtime attachment creation
- directory checks
- virtual kernel state files under `/dvc`/`/dev` and `/prc`/`/proc`
- global sync
- attachment enumeration

## Shell Impact

The shell no longer talks to FAT32 or RAMFS directly.

It uses VFS paths and command manifests in `/cmd` with `/boot/cmd` fallback. Command binaries are loaded from `/bin` with `/boot/bin` fallback.
