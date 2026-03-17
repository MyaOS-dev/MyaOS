#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 5 ]]; then
  cat >&2 <<'USAGE'
usage: tools/mkpkg.sh <out.pkg> <name> <version> <abi> <target_path=host_file>...
example:
  tools/mkpkg.sh demo.pkg demo 1.0.0 10200 \
    /bin/demo.elf=build/programs/system/about.elf \
    /cmd/demo.cmd=programs/cmd/about.cmd
USAGE
  exit 1
fi

out_pkg="$1"
name="$2"
version="$3"
abi="$4"
shift 4

if [[ ! "$name" =~ ^[A-Za-z0-9_.-]+$ ]]; then
  echo "mkpkg: invalid package name: $name" >&2
  exit 1
fi
if [[ ! "$version" =~ ^[0-9]+(\.[0-9]+){0,2}$ ]]; then
  echo "mkpkg: invalid version: $version" >&2
  exit 1
fi
if [[ ! "$abi" =~ ^[0-9]+$ ]]; then
  echo "mkpkg: invalid abi: $abi" >&2
  exit 1
fi

{
  echo "MYAPKG1"
  echo "name=$name"
  echo "version=$version"
  echo "abi=$abi"

  for spec in "$@"; do
    target_path="${spec%%=*}"
    host_file="${spec#*=}"

    if [[ "$spec" != *=* ]]; then
      echo "mkpkg: bad mapping '$spec' (expected target=host)" >&2
      exit 1
    fi
    if [[ ! "$target_path" =~ ^/ ]]; then
      echo "mkpkg: target path must be absolute: $target_path" >&2
      exit 1
    fi
    if [[ ! -f "$host_file" ]]; then
      echo "mkpkg: missing host file: $host_file" >&2
      exit 1
    fi

    echo "file=$target_path"
    echo -n "hex="
    xxd -p -c 0 "$host_file" | tr -d '\n'
    echo
  done
} > "$out_pkg"

echo "mkpkg: wrote $out_pkg"
