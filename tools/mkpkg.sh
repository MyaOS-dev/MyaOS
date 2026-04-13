#!/usr/bin/env bash
set -euo pipefail

PKG_MAX_BYTES=$((512 * 1024))
FORMAT="mpkg"
ARCH="x86_64"
DESCRIPTION=""
SIGNED=0
SCRIPT_PRE=""
SCRIPT_POST=""
declare -a DEPENDS=()
declare -a PROVIDES=()
declare -a TARGET_PATHS=()
declare -a HOST_FILES=()

print_usage() {
  cat >&2 <<'USAGE'
usage:
  tools/mkpkg.sh [options] <out.pkg> <name> <version> <abi> <target_path=host_file>...

formats:
  default format is binary MPKG
  --legacy-text emits old MYAPKG1 text format

options:
  --arch <arch>           metadata arch string (default: x86_64)
  --description <text>    package description
  --depends <spec>        dependency (repeatable)
  --provides <spec>       provide/capability (repeatable)
  --script-pre <file>     pre-install script file
  --script-post <file>    post-install script file
  --signed                set MPKG signed flag
  --legacy-text           output MYAPKG1 text package
  -h, --help              show this help

example:
  tools/mkpkg.sh --description "demo app" demo.pkg demo 1.0.0 10200 \
    /bin/demo.elf=build/programs/system/about.elf \
    /cmd/demo.cmd=programs/cmd/about.cmd

note:
  package size must not exceed 524288 bytes (installer limit).
USAGE
}

calc_sha256() {
  local file="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum -- "$file" | awk '{print $1}'
    return 0
  fi
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 -- "$file" | awk '{print $1}'
    return 0
  fi
  if command -v openssl >/dev/null 2>&1; then
    openssl dgst -sha256 -- "$file" | awk '{print $NF}'
    return 0
  fi
  return 1
}

validate_inputs() {
  local name="$1"
  local version="$2"
  local abi="$3"
  local arch="$4"
  local description="$5"
  local script_pre="$6"
  local script_post="$7"

  if [[ ! "$name" =~ ^[A-Za-z0-9_.-]+$ ]]; then
    echo "mkpkg: invalid package name: $name" >&2
    return 1
  fi
  if (( ${#name} > 31 )); then
    echo "mkpkg: package name too long (max 31 bytes)" >&2
    return 1
  fi
  if [[ ! "$version" =~ ^[0-9]+(\.[0-9]+){0,2}$ ]]; then
    echo "mkpkg: invalid version: $version" >&2
    return 1
  fi
  if (( ${#version} > 23 )); then
    echo "mkpkg: version too long (max 23 bytes)" >&2
    return 1
  fi
  if [[ ! "$abi" =~ ^[0-9]+$ ]]; then
    echo "mkpkg: invalid abi: $abi" >&2
    return 1
  fi
  if [[ ! "$arch" =~ ^[A-Za-z0-9_.-]+$ ]]; then
    echo "mkpkg: invalid arch: $arch" >&2
    return 1
  fi
  if (( ${#arch} > 31 )); then
    echo "mkpkg: arch too long (max 31 bytes)" >&2
    return 1
  fi
  if (( ${#description} > 255 )); then
    echo "mkpkg: description too long (max 255 bytes)" >&2
    return 1
  fi

  if [[ -n "$script_pre" && ! -f "$script_pre" ]]; then
    echo "mkpkg: missing --script-pre file: $script_pre" >&2
    return 1
  fi
  if [[ -n "$script_post" && ! -f "$script_post" ]]; then
    echo "mkpkg: missing --script-post file: $script_post" >&2
    return 1
  fi
}

emit_legacy_text_pkg() {
  local out="$1"
  local name="$2"
  local version="$3"
  local abi="$4"

  {
    echo "MYAPKG1"
    echo "name=$name"
    echo "version=$version"
    echo "abi=$abi"

    for idx in "${!TARGET_PATHS[@]}"; do
      local target_path="${TARGET_PATHS[$idx]}"
      local host_file="${HOST_FILES[$idx]}"
      local file_sha256

      echo "file=$target_path"
      file_sha256="$(calc_sha256 "$host_file")" || {
        echo "mkpkg: failed to calculate sha256 for: $host_file" >&2
        return 1
      }
      if [[ ! "$file_sha256" =~ ^[A-Fa-f0-9]{64}$ ]]; then
        echo "mkpkg: invalid sha256 output for: $host_file" >&2
        return 1
      fi
      echo "sha256=$file_sha256"
      echo -n "hex="
      xxd -p -c 0 -- "$host_file" | tr -d '\n'
      echo
    done
  } > "$out"
}

emit_mpkg_binary() {
  local out="$1"
  local name="$2"
  local version="$3"
  local abi="$4"
  local arch="$5"
  local description="$6"
  local signed="$7"
  local script_pre="$8"
  local script_post="$9"
  local entries_tsv="${10}"
  local depends_txt="${11}"
  local provides_txt="${12}"

  if ! command -v python3 >/dev/null 2>&1; then
    echo "mkpkg: python3 is required for MPKG generation" >&2
    return 1
  fi

  python3 - "$out" "$name" "$version" "$abi" "$arch" "$description" "$signed" \
    "$script_pre" "$script_post" "$entries_tsv" "$depends_txt" "$provides_txt" <<'PY'
import os
import struct
import sys
import zlib

MPKG_VERSION = 1
MPKG_HEADER_SIZE = 64
MPKG_FILE_ENTRY_SIZE = 40
MPKG_FILETABLE_HEAD_SIZE = 8

MPKG_FLAG_SIGNED = 1 << 1

MPKG_META_NAME = 1
MPKG_META_VERSION = 2
MPKG_META_ARCH = 3
MPKG_META_ABI = 4
MPKG_META_DEPENDS = 5
MPKG_META_PROVIDES = 6
MPKG_META_DESCRIPTION = 7
MPKG_META_SCRIPT_PRE = 8
MPKG_META_SCRIPT_POST = 9

MPKG_FILE_FLAG_EXEC = 1 << 0

(
    out_path,
    pkg_name,
    pkg_version,
    pkg_abi_text,
    pkg_arch,
    pkg_description,
    signed_text,
    script_pre_path,
    script_post_path,
    entries_tsv,
    depends_txt,
    provides_txt,
) = sys.argv[1:13]

pkg_abi = int(pkg_abi_text)
flags = MPKG_FLAG_SIGNED if signed_text == "1" else 0

def load_lines(path: str):
    items = []
    with open(path, "r", encoding="utf-8", newline="") as fh:
        for raw in fh:
            text = raw.rstrip("\n")
            if text:
                items.append(text)
    return items

def load_script(path: str):
    if not path:
        return b""
    with open(path, "rb") as fh:
        data = fh.read()
    if b"\x00" in data:
        raise SystemExit(f"mkpkg: script contains NUL byte: {path}")
    if len(data) > 1023:
        raise SystemExit(f"mkpkg: script too large for runtime limit (1023): {path}")
    return data

def add_tlv(meta: bytearray, tlv_type: int, payload: bytes):
    if not payload:
        return
    meta.extend(struct.pack("<HHI", tlv_type, 0, len(payload)))
    meta.extend(payload)

def fnv1a64(payload: bytes):
    h = 0xCBF29CE484222325
    for b in payload:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h

depends = load_lines(depends_txt)
provides = load_lines(provides_txt)
script_pre = load_script(script_pre_path)
script_post = load_script(script_post_path)

metadata = bytearray()
add_tlv(metadata, MPKG_META_NAME, pkg_name.encode("utf-8"))
add_tlv(metadata, MPKG_META_VERSION, pkg_version.encode("utf-8"))
add_tlv(metadata, MPKG_META_ARCH, pkg_arch.encode("utf-8"))
add_tlv(metadata, MPKG_META_ABI, struct.pack("<I", pkg_abi))
if depends:
    add_tlv(metadata, MPKG_META_DEPENDS, b"\x00".join(x.encode("utf-8") for x in depends))
if provides:
    add_tlv(metadata, MPKG_META_PROVIDES, b"\x00".join(x.encode("utf-8") for x in provides))
if pkg_description:
    add_tlv(metadata, MPKG_META_DESCRIPTION, pkg_description.encode("utf-8"))
if script_pre:
    add_tlv(metadata, MPKG_META_SCRIPT_PRE, script_pre)
if script_post:
    add_tlv(metadata, MPKG_META_SCRIPT_POST, script_post)

entries = []
strtab = bytearray()
data_block = bytearray()

with open(entries_tsv, "r", encoding="utf-8", newline="") as fh:
    for line_no, raw in enumerate(fh, start=1):
        row = raw.rstrip("\n")
        if not row:
            continue
        parts = row.split("\t")
        if len(parts) != 2:
            raise SystemExit(f"mkpkg: bad entries row #{line_no}")
        target_path, host_path = parts
        with open(host_path, "rb") as payload_fh:
            payload = payload_fh.read()

        path_off = len(strtab)
        strtab.extend(target_path.encode("utf-8"))
        strtab.append(0)

        data_off = len(data_block)
        data_block.extend(payload)

        is_exec = os.access(host_path, os.X_OK)
        entry_flags = MPKG_FILE_FLAG_EXEC if is_exec else 0
        mode = 0o755 if is_exec else 0o644
        quick_hash = fnv1a64(payload)
        entries.append((path_off, data_off, len(payload), entry_flags, mode, quick_hash))

filetable = bytearray()
filetable.extend(struct.pack("<II", len(entries), len(strtab)))
for entry in entries:
    filetable.extend(struct.pack("<QQQIIQ", *entry))
filetable.extend(strtab)

metadata_off = MPKG_HEADER_SIZE
filetable_off = metadata_off + len(metadata)
data_off = filetable_off + len(filetable)
pkg_size = data_off + len(data_block)

header = bytearray(MPKG_HEADER_SIZE)
header[0:4] = b"MPKG"
struct.pack_into("<H", header, 4, MPKG_VERSION)
struct.pack_into("<H", header, 6, MPKG_HEADER_SIZE)
struct.pack_into("<I", header, 8, flags)
struct.pack_into("<Q", header, 12, metadata_off)
struct.pack_into("<Q", header, 20, filetable_off)
struct.pack_into("<Q", header, 28, data_off)
struct.pack_into("<Q", header, 36, pkg_size)

pkg = bytearray()
pkg.extend(header)
pkg.extend(metadata)
pkg.extend(filetable)
pkg.extend(data_block)

crc_buf = bytearray(pkg)
crc_buf[44:48] = b"\x00\x00\x00\x00"
crc32 = zlib.crc32(crc_buf) & 0xFFFFFFFF
struct.pack_into("<I", pkg, 44, crc32)

with open(out_path, "wb") as out_fh:
    out_fh.write(pkg)
PY
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --legacy-text)
      FORMAT="legacy"
      shift
      ;;
    --arch)
      [[ $# -ge 2 ]] || { echo "mkpkg: --arch requires value" >&2; exit 1; }
      ARCH="$2"
      shift 2
      ;;
    --description)
      [[ $# -ge 2 ]] || { echo "mkpkg: --description requires value" >&2; exit 1; }
      DESCRIPTION="$2"
      shift 2
      ;;
    --depends)
      [[ $# -ge 2 ]] || { echo "mkpkg: --depends requires value" >&2; exit 1; }
      DEPENDS+=("$2")
      shift 2
      ;;
    --provides)
      [[ $# -ge 2 ]] || { echo "mkpkg: --provides requires value" >&2; exit 1; }
      PROVIDES+=("$2")
      shift 2
      ;;
    --script-pre)
      [[ $# -ge 2 ]] || { echo "mkpkg: --script-pre requires file path" >&2; exit 1; }
      SCRIPT_PRE="$2"
      shift 2
      ;;
    --script-post)
      [[ $# -ge 2 ]] || { echo "mkpkg: --script-post requires file path" >&2; exit 1; }
      SCRIPT_POST="$2"
      shift 2
      ;;
    --signed)
      SIGNED=1
      shift
      ;;
    -h|--help)
      print_usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -*)
      echo "mkpkg: unknown option: $1" >&2
      print_usage
      exit 1
      ;;
    *)
      break
      ;;
  esac
done

if [[ $# -lt 5 ]]; then
  print_usage
  exit 1
fi

out_pkg="$1"
name="$2"
version="$3"
abi="$4"
shift 4

validate_inputs "$name" "$version" "$abi" "$ARCH" "$DESCRIPTION" "$SCRIPT_PRE" "$SCRIPT_POST"

for dep in "${DEPENDS[@]}"; do
  if [[ ! "$dep" =~ ^[A-Za-z0-9_.-]+([<>=~!][^[:space:]]+)?$ ]]; then
    echo "mkpkg: suspicious dependency spec: $dep" >&2
    exit 1
  fi
  if (( ${#dep} > 31 )); then
    echo "mkpkg: dependency spec too long for runtime parser (max 31): $dep" >&2
    exit 1
  fi
done

for prov in "${PROVIDES[@]}"; do
  if [[ ! "$prov" =~ ^[A-Za-z0-9_.-]+$ ]]; then
    echo "mkpkg: invalid provides spec: $prov" >&2
    exit 1
  fi
  if (( ${#prov} > 31 )); then
    echo "mkpkg: provides entry too long (max 31): $prov" >&2
    exit 1
  fi
done

for spec in "$@"; do
  target_path="${spec%%=*}"
  host_file="${spec#*=}"

  if [[ "$spec" != *=* ]]; then
    echo "mkpkg: bad mapping '$spec' (expected target=host)" >&2
    exit 1
  fi
  if [[ -z "$target_path" || -z "$host_file" ]]; then
    echo "mkpkg: bad mapping '$spec' (empty target or host part)" >&2
    exit 1
  fi
  if [[ ! "$target_path" =~ ^/ ]]; then
    echo "mkpkg: target path must be absolute: $target_path" >&2
    exit 1
  fi
  if [[ "$target_path" == *$'\n'* || "$target_path" == *$'\r'* || "$target_path" == *$'\t'* ]]; then
    echo "mkpkg: target path must not contain control separators: $target_path" >&2
    exit 1
  fi
  if [[ "$host_file" == *$'\n'* || "$host_file" == *$'\r'* || "$host_file" == *$'\t'* ]]; then
    echo "mkpkg: host path must not contain control separators: $host_file" >&2
    exit 1
  fi
  if [[ ! -f "$host_file" ]]; then
    echo "mkpkg: missing host file: $host_file" >&2
    exit 1
  fi

  TARGET_PATHS+=("$target_path")
  HOST_FILES+=("$host_file")
done

if (( ${#TARGET_PATHS[@]} == 0 )); then
  echo "mkpkg: at least one file mapping is required" >&2
  exit 1
fi

out_dir="$(dirname -- "$out_pkg")"
mkdir -p -- "$out_dir"
tmp_dir="$(mktemp -d "${out_dir}/.mkpkg.XXXXXX")"
tmp_pkg="${tmp_dir}/out.pkg"
tmp_entries="${tmp_dir}/entries.tsv"
tmp_depends="${tmp_dir}/depends.txt"
tmp_provides="${tmp_dir}/provides.txt"
cleanup() {
  rm -rf -- "$tmp_dir"
}
trap cleanup EXIT

: > "$tmp_entries"
for idx in "${!TARGET_PATHS[@]}"; do
  printf '%s\t%s\n' "${TARGET_PATHS[$idx]}" "${HOST_FILES[$idx]}" >> "$tmp_entries"
done

: > "$tmp_depends"
for dep in "${DEPENDS[@]}"; do
  printf '%s\n' "$dep" >> "$tmp_depends"
done

: > "$tmp_provides"
for prov in "${PROVIDES[@]}"; do
  printf '%s\n' "$prov" >> "$tmp_provides"
done

if [[ "$FORMAT" == "legacy" ]]; then
  emit_legacy_text_pkg "$tmp_pkg" "$name" "$version" "$abi"
else
  emit_mpkg_binary "$tmp_pkg" "$name" "$version" "$abi" "$ARCH" "$DESCRIPTION" "$SIGNED" \
    "$SCRIPT_PRE" "$SCRIPT_POST" "$tmp_entries" "$tmp_depends" "$tmp_provides"
fi

pkg_size="$(wc -c < "$tmp_pkg" | tr -d '[:space:]')"
if [[ ! "$pkg_size" =~ ^[0-9]+$ ]]; then
  echo "mkpkg: failed to determine package size for: $tmp_pkg" >&2
  exit 1
fi
if (( pkg_size > PKG_MAX_BYTES )); then
  echo "mkpkg: package too large: ${pkg_size} bytes (limit ${PKG_MAX_BYTES})" >&2
  exit 1
fi

mv -f -- "$tmp_pkg" "$out_pkg"
if [[ "$FORMAT" == "legacy" ]]; then
  echo "mkpkg: wrote legacy MYAPKG1 $out_pkg"
else
  echo "mkpkg: wrote MPKG $out_pkg"
fi
