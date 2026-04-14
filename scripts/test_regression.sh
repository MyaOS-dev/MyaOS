#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

./scripts/test_smoke.sh
./scripts/test_baseline.sh
./scripts/test_ntfs.sh

required_docs=(
  "README.md"
  "docs/ABI.md"
  "docs/SUBSYSTEM_STATUS.md"
  "docs/SYSCALLS_OFFICIAL.md"
  "docs/LINUX_COMPAT_MATRIX.md"
  "docs/DIAGNOSTICS.md"
)

for doc in "${required_docs[@]}"; do
  if [[ ! -f "$doc" ]]; then
    echo "missing required doc: $doc" >&2
    exit 1
  fi
done

python - <<'PY'
import glob
import os
import re
import sys

exec_map = {}
manifest_text = {}
for path in sorted(glob.glob("programs/cmd/*.cmd")):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        first = f.readline().strip()
        body = f.read()

    name = os.path.basename(path)
    manifest_text[name] = (first + "\n" + body).lower()
    if not first:
        print(f"manifest has empty first line: {path}", file=sys.stderr)
        sys.exit(1)
    if first.startswith("exec="):
        exec_path = first[5:]
    else:
        exec_path = first
    if not exec_path.startswith("/"):
        print(f"manifest first line must be absolute path or exec=absolute: {path}: {first}", file=sys.stderr)
        sys.exit(1)

    exec_map.setdefault(exec_path, set()).add(name)

    if name == "compat.cmd" and "legacy" not in body.lower():
        print("compat.cmd must explicitly be marked legacy", file=sys.stderr)
        sys.exit(1)

for exec_path, names in exec_map.items():
    if len(names) <= 1:
        continue
    base = os.path.basename(exec_path)
    if base.endswith(".elf"):
        base = base[:-4]
    canonical = base + ".cmd"
    if canonical not in names:
        print(f"duplicate exec target without canonical manifest {canonical}: {exec_path} -> {sorted(names)}", file=sys.stderr)
        sys.exit(1)
    for name in sorted(names):
        if name == canonical:
            continue
        text = manifest_text.get(name, "")
        if ("alias" not in text) and ("compatibility" not in text) and ("legacy" not in text):
            print(f"duplicate manifest {name} for {exec_path} must be marked as alias/legacy", file=sys.stderr)
            sys.exit(1)

print("manifest regression checks passed")
PY

echo "regression tests passed"
