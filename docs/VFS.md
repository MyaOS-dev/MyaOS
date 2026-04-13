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
- `ntfs`
- `ramfs`
- `myafs`

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

## Runtime Writeback Note

Firmware-loaded block images are copied into RAM before the kernel takes over. After `ExitBootServices`, filesystem writes can still update that runtime image, but flushing changes back through UEFI Block I/O is not guaranteed.

Current consequence:

- `fat32`, `ext*`, and `ntfs` writes work during the active session
- persistence back to the original firmware-backed disk image is not guaranteed yet
- `ramfs` and `myafs` are unaffected because they do not depend on UEFI Block I/O writeback
