# Package and Repository

## Package Format (`MYAPKG1`)

MyaOS packages are text files with a deterministic line-based format:

```text
MYAPKG1
name=<package_name>
version=<semver>
abi=<abi_version_uint>
file=<absolute_target_path>
hex=<payload_hex>
file=<absolute_target_path>
hex=<payload_hex>
...
```

Rules:

- `name` must match `[A-Za-z0-9_.-]+`.
- `version` is semantic version (`X.Y` or `X.Y.Z`).
- `abi` is a numeric ABI level compared against `MYAOS_ABI_VERSION`.
- Each `file=` line must be immediately followed by matching `hex=` payload line.
- `hex=` payload is lowercase/uppercase hexadecimal bytes (no separators).

The package manager rejects a package when `abi` is newer than the running system ABI.

## Repository Index Format

Repository index is also text:

```text
# MYAOS_REPO1
# name|version|package_path|abi
hello|1.0.0|/repo/hello-1.0.0.pkg|10200
```

Fields:

- `name`: package id
- `version`: semantic version
- `package_path`: path to `.pkg` file in current filesystem
- `abi`: minimum/target ABI required by package

Default repository index path used by commands is `/repo/index.pkg`.

## User Commands

- `pkg install <pkg_path>`: install package from file
- `pkg list`: list installed packages
- `pkg info <name>`: show installed package metadata
- `pkg avail [index]`: list repo index entries
- `pkg install-from <name> [index]`: install latest compatible package by name
- `pkg upgrade [index]`: upgrade installed packages from repository
- `pkg repo-init [index]`: initialize repository index file
- `pkg repo-add <index> <name> <version> <pkg_path> [abi]`: add/update entry
- `update [index]`: wrapper for `pkg upgrade`

Installed package DB is stored in `/var/pkg/installed.db` and per-package metadata in `/var/pkg/<name>.meta`.

## Host-side Package Build

Use helper script:

```bash
tools/mkpkg.sh <out.pkg> <name> <version> <abi> <target_path=host_file>...
```

Example:

```bash
tools/mkpkg.sh hello.pkg hello 1.0.0 10200 \
  /bin/hello.elf=build/programs/system/about.elf \
  /cmd/hello.cmd=programs/cmd/about.cmd
```
