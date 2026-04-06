# MyaFS Host Driver (Linux/macOS/Windows)

MyaOS now includes a portable host-side `MyaFS` driver/tool under `tools/myafs_driver`.

It is a userspace driver library + CLI utility that lets you create and edit `MyaFS` images on:

- Linux
- macOS
- Windows

For Linux host systems, a kernel-level filesystem module is also provided in `tools/myafs_host_linux`.

## Build

Linux/macOS:

```bash
./tools/myafs_driver/build.sh
```

Optional install into PATH (`/usr/local/sbin/mkfs.myafs`):

```bash
sudo make install-mkfs-myafs
```

This builds:

- `tools/myafs_driver/myafsdrv`
- `tools/myafs_driver/mkfs.myafs`

Windows (Developer Command Prompt):

```bat
cd tools\myafs_driver
build.bat
```

## Usage

```bash
tools/myafs_driver/mkfs.myafs -L MyaDisk mydisk.mya
tools/myafs_driver/myafsdrv init mydisk.mya MyaDisk
tools/myafs_driver/myafsdrv mkdir mydisk.mya /cmd
tools/myafs_driver/myafsdrv touch mydisk.mya /cmd/readme.txt
tools/myafs_driver/myafsdrv write mydisk.mya /cmd/readme.txt hello from host
tools/myafs_driver/myafsdrv list mydisk.mya /
tools/myafs_driver/myafsdrv cat mydisk.mya /cmd/readme.txt
tools/myafs_driver/myafsdrv del mydisk.mya /cmd/readme.txt
```

`mkfs.myafs` formats:

- `--format legacy` (default): current text-based `MYAFS1` image format (fully supported by `myafsdrv` and host mount module)
- `--format v2`: new binary MyaFS on-disk layout scaffold (superblock ring + checkpoint package), used to start the next-generation format work
- for block devices (for example `/dev/sdb`), format auto-selects to `v2`

Examples:

```bash
sudo tools/myafs_driver/mkfs.myafs /dev/sdb
sudo tools/myafs_driver/mkfs.myafs --format v2 -L DataDisk /dev/sdb

# if installed:
sudo mkfs.myafs /dev/sdb
```

## Notes

- This is a host-side driver/tooling layer for `MyaFS` images.
- It does not require kernel module installation on host OSes.
- The in-kernel `MyaFS` backend is still in `kernel/fs/vfs_myafs.c`.

## Linux Kernel Module

Path: `tools/myafs_host_linux`

Build:

```bash
make myafs-host-kmod
```

Mount example:

```bash
sudo insmod tools/myafs_host_linux/myafs_linux.ko
sudo mount -t myafs /absolute/path/to/disk.mya /mnt/myafs
```

Block-device mount example:

```bash
sudo mount -t myafs /dev/sdb /mnt/myafs
```

Legacy option form:

```bash
sudo mount -t myafs none /mnt/myafs -o image=/absolute/path/to/disk.mya
```

Important: use `-t myafs`; plain `mount /dev/sdb /mnt` relies on auto-detection and usually fails for custom filesystems.

DKMS auto-install/upgrade (recommended):

```bash
sudo pacman -S --needed linux-headers
sudo make myafs-host-dkms-install
sudo modprobe myafs_linux
```

DKMS removal:

```bash
sudo make myafs-host-dkms-remove
```

The DKMS package is versioned with `tools/myafs_host_linux/VERSION` and set to `AUTOINSTALL="yes"` in `tools/myafs_host_linux/dkms.conf`, so rebuilds are triggered for new host kernels.
When module sources change, bump that `VERSION` value and rerun `myafs-host-dkms-install`.
If headers are in a non-standard location, use `tools/myafs_host_linux/dkms-install.sh --kernelsourcedir <path>`.
