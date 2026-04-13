# Package and Repository

## Package Format (`MPKG`)

Default package format in MyaOS is binary `MPKG`:

```c
typedef struct {
    char     magic[4];      /* "MPKG" */
    uint16_t version;       /* 1 */
    uint16_t header_size;   /* 64 */
    uint32_t flags;         /* signed/compressed/delta */
    uint64_t metadata_off;  /* TLV metadata block */
    uint64_t filetable_off; /* file table + string table */
    uint64_t data_off;      /* raw file data block */
    uint64_t pkg_size;      /* full package size */
    uint32_t crc32;         /* crc32 of whole file, crc field treated as 0 */
    uint32_t reserved;
} mpkg_header_t;
```

File layout:

- `[header:64]`
- `[metadata TLV]`
- `[file table]`
- `[data block]`

Metadata is TLV (`type:u16`, `reserved:u16`, `length:u32`, `data[]`).

Supported metadata types:

- `1`: name
- `2`: version
- `3`: arch
- `4`: abi (`u32` little-endian or text)
- `5`: depends list
- `6`: provides list
- `7`: description
- `8`: pre-install script
- `9`: post-install script

File table format:

- `u32 entry_count`
- `u32 strtab_size`
- `entry_count` records of:
  - `u64 path_off` (into string table)
  - `u64 data_off` (into data block)
  - `u64 size`
  - `u32 flags` (`exec/config/dir`)
  - `u32 mode`
  - `u64 hash` (quick hash)
- string table (`\0`-terminated absolute paths)

Runtime behavior:

- Installer auto-detects format: `MPKG` first, then legacy text `MYAPKG1`.
- Package is rejected if `abi` is newer than running `MYAOS_ABI_VERSION`.
- `depends` are checked against installed package names before install.
- `script-pre` runs before extracting files, `script-post` after.
- Files written to `/lib/modules/*.kmod` are auto-loaded as kernel modules.

## Legacy Format (`MYAPKG1`)

Legacy text packages are still supported for compatibility:

```text
MYAPKG1
name=<package_name>
version=<semver>
abi=<abi_version_uint>
file=<absolute_target_path>
sha256=<sha256_hex_of_payload>
hex=<payload_hex>
...
```

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

- `pkg install <pkg_path|pkg_url>`: install package from local file or network URL
- `pkg remove <name>`: remove installed package files and metadata
- `pkg search <pattern> [index]`: search installed entries and repository index
- `pkg list`: list installed packages
- `pkg info <name>`: show installed package metadata
- `pkg avail [index]`: list repo index entries (`index` can also be URL)
- `pkg install-from <name> [index]`: install latest compatible package by name (`index` can also be URL)
- `pkg upgrade [index]`: upgrade installed packages from repository (`index` can also be URL)
- `pkg downgrade <name> <version> [index]`: install selected older version (`index` can also be URL)
- `pkg dry-run <install|install-from|upgrade|downgrade|remove> ...`: preview changes without writing files
- `pkg repo-init [index]`: initialize repository index file
- `pkg repo-add <index> <name> <version> <pkg_path_or_url> [abi]`: add/update entry
- `update [index]`: wrapper for `pkg upgrade`

Installed package DB is stored in `/var/pkg/installed.db` and per-package metadata in `/var/pkg/<name>.meta`.
For `https://` sources, downloader uses native TLS in `curl` directly; optional HTTP forward proxy mode is available in `curl` via `--proxy`.

## Host-side Package Build

Use helper script:

```bash
tools/mkpkg.sh [options] <out.pkg> <name> <version> <abi> <target_path=host_file>...
```

Binary `MPKG` example:

```bash
tools/mkpkg.sh \
  --arch x86_64 \
  --description "hello package" \
  --depends libc \
  --provides hello-cli \
  hello.pkg hello 1.0.0 10200 \
  /bin/hello.elf=build/programs/system/about.elf \
  /cmd/hello.cmd=programs/cmd/about.cmd
```

Legacy text package example:

```bash
tools/mkpkg.sh --legacy-text hello-legacy.pkg hello 1.0.0 10200 \
  /bin/hello.elf=build/programs/system/about.elf
```

`--script-pre <file>` and `--script-post <file>` embed install scripts into `MPKG`.
