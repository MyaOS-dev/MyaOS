# MyaFS Linux Kernel Module (Host)

This is a host-side Linux kernel filesystem module for mounting MyaFS images.

Current status:

- Kernel-level module (`myafs_linux.ko`)
- Read-write mount support
- Supports direct source mount (`mount -t myafs IMAGE MOUNTPOINT`)
- Also supports legacy `-o image=/path/to/image.mya`
- Parses the same `MYAFS1` line-based image format as `tools/myafs_driver`

## Build

```bash
cd tools/myafs_host_linux
make
```

Requires installed kernel headers for your running kernel (`/lib/modules/$(uname -r)/build`).

On Arch Linux:

```bash
sudo pacman -S --needed linux-headers
```

## DKMS (Auto Rebuild on Kernel Updates)

This module includes a DKMS layout (`dkms.conf` + `VERSION`) so it can be rebuilt automatically when host kernels are updated.

Install/register with DKMS:

```bash
cd tools/myafs_host_linux
sudo ./dkms-install.sh
```

Build for a specific kernel and/or custom source tree:

```bash
sudo ./dkms-install.sh --kernelver 6.19.6-arch1-1 --kernelsourcedir /usr/src/linux
```

Or from the repository root:

```bash
sudo make myafs-host-dkms-install
```

Remove from DKMS:

```bash
cd tools/myafs_host_linux
sudo ./dkms-remove.sh
```

After install, load with:

```bash
sudo modprobe myafs_linux
```

When you change module sources, bump `VERSION` and run `sudo ./dkms-install.sh` again to register the new package version.

## Load / Mount

```bash
sudo insmod myafs_linux.ko
sudo mkdir -p /mnt/myafs
sudo mount -t myafs /absolute/path/to/disk.mya /mnt/myafs
```

For block devices:

```bash
sudo mount -t myafs /dev/sdb /mnt/myafs
```

Legacy option form (still supported):

```bash
sudo mount -t myafs none /mnt/myafs -o image=/absolute/path/to/disk.mya
```

## Unmount / Unload

```bash
sudo umount /mnt/myafs
sudo rmmod myafs_linux
```

Notes:

- Use `-t myafs`; plain `mount /dev/sdb /mnt` usually fails because auto-detection does not know custom filesystems.
- `MYAFS1` images are parsed fully and support create/write/delete operations.
- `v2` images are mounted through a compatibility metadata payload stored in the v2 data area (full native v2 tree walking is still in progress).
