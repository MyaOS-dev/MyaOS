#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

OVMF_CODE="${OVMF_CODE:-/usr/share/edk2/x64/OVMF_CODE.4m.fd}"
OVMF_VARS="${OVMF_VARS:-.ovmf/OVMF_VARS.4m.fd}"
OVMF_VARS_TEMPLATE="${OVMF_VARS_TEMPLATE:-/usr/share/edk2/x64/OVMF_VARS.4m.fd}"

if [[ ! -f "$OVMF_CODE" ]]; then
  echo "missing OVMF code file: $OVMF_CODE" >&2
  exit 1
fi

make clean >/dev/null
make -j4 MYAOS_DEBUG=1 build/esp.img >/dev/null

if [[ ! -f "$OVMF_VARS" ]]; then
  if [[ ! -f "$OVMF_VARS_TEMPLATE" ]]; then
    echo "missing OVMF vars template: $OVMF_VARS_TEMPLATE" >&2
    exit 1
  fi
  mkdir -p "$(dirname "$OVMF_VARS")"
  cp "$OVMF_VARS_TEMPLATE" "$OVMF_VARS"
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

cat >"$tmp_dir/autorun.sh" <<'EOF'
debug on
sh -c "help"
ls /boot
cat /boot/autorun.sh
pkg list
poweroff
EOF

mcopy -o -i build/esp.img "$tmp_dir/autorun.sh" ::/autorun.sh

log_file="$tmp_dir/qemu-baseline.log"
qemu_rc=0
if timeout 90s qemu-system-x86_64 \
  -machine q35 \
  -m 1G \
  -display none \
  -serial none \
  -debugcon "file:$log_file" \
  -global isa-debugcon.iobase=0xe9 \
  -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
  -drive if=pflash,format=raw,file="$OVMF_VARS" \
  -drive file=build/esp.img,format=raw; then
  qemu_rc=0
else
  qemu_rc=$?
fi

if [[ "$qemu_rc" -ne 0 && "$qemu_rc" -ne 124 ]]; then
  echo "qemu baseline failed with rc=$qemu_rc" >&2
  [[ -f "$log_file" ]] && strings "$log_file" | tail -n 200 >&2
  exit 1
fi

if [[ ! -f "$log_file" ]]; then
  echo "baseline log was not produced" >&2
  exit 1
fi

required_cmds=(
  "sh -c \"help\""
  "ls /boot"
  "cat /boot/autorun.sh"
  "pkg list"
)

for cmd in "${required_cmds[@]}"; do
  if ! rg -a -q "\\[debug\\] script exec: ${cmd}" "$log_file"; then
    echo "baseline command was not executed: ${cmd}" >&2
    strings "$log_file" | tail -n 200 >&2
    exit 1
  fi
done

required_spawns=(
  "/bin/sh argc=3"
  "/bin/ls argc=2"
  "/bin/cat argc=2"
  "/bin/pkg argc=2"
)

for spawn in "${required_spawns[@]}"; do
  if ! rg -a -q "\\[debug\\] spawn ${spawn}" "$log_file"; then
    echo "baseline executable was not spawned via expected /bin path: ${spawn}" >&2
    strings "$log_file" | tail -n 200 >&2
    exit 1
  fi
done

if rg -a -q "PANIC|vector=|unknown command|spawn failed|wait failed|error:" "$log_file"; then
  echo "baseline log contains failure markers" >&2
  strings "$log_file" | tail -n 200 >&2
  exit 1
fi

if rg -a -q "\\[debug\\] pid [0-9]+ exit=-?[1-9][0-9]*" "$log_file"; then
  echo "baseline command exited with non-zero code" >&2
  strings "$log_file" | tail -n 200 >&2
  exit 1
fi

echo "baseline tests passed"
