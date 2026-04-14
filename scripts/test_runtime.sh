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
make -j4 MYAOS_DEBUG=1 >/dev/null

if [[ ! -f "$OVMF_VARS" ]]; then
  if [[ ! -f "$OVMF_VARS_TEMPLATE" ]]; then
    echo "missing OVMF vars file: $OVMF_VARS" >&2
    exit 1
  fi
  mkdir -p "$(dirname "$OVMF_VARS")"
  cp "$OVMF_VARS_TEMPLATE" "$OVMF_VARS"
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

cat >"$tmp_dir/autorun.sh" <<'EOF'
debug on
debug bad && load /boot/bin/fail.elf
debug bad || load /boot/bin/about.elf
debug on || load /boot/bin/fail.elf
load /boot/bin/ok.elf && load /boot/bin/about.elf
load /boot/bin/less.elf /boot/autorun.sh --keys=jfkbGgq
load /boot/bin/about.elf where
load /boot/bin/about.elf debug
load /boot/bin/meminfo.elf
load /boot/bin/tasks.elf
load /boot/bin/p0check.elf
load /boot/bin/whoami.elf
load /boot/bin/abi.elf
load /boot/bin/seccheck.elf
poweroff
EOF

mcopy -o -i build/esp.img "$tmp_dir/autorun.sh" ::/autorun.sh

log_file="$tmp_dir/qemu-runtime.log"
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
  echo "qemu runtime failed with rc=$qemu_rc" >&2
  [[ -f "$log_file" ]] && tail -n 200 "$log_file" >&2
  exit 1
fi

if [[ ! -f "$log_file" ]]; then
  echo "runtime log was not produced" >&2
  exit 1
fi

required_cmds=(
  "debug bad && load /boot/bin/fail.elf"
  "debug bad || load /boot/bin/about.elf"
  "debug on || load /boot/bin/fail.elf"
  "load /boot/bin/ok.elf && load /boot/bin/about.elf"
  "load /boot/bin/less.elf /boot/autorun.sh --keys=jfkbGgq"
  "load /boot/bin/about.elf where"
  "load /boot/bin/about.elf debug"
  "load /boot/bin/meminfo.elf"
  "load /boot/bin/tasks.elf"
  "load /boot/bin/p0check.elf"
  "load /boot/bin/whoami.elf"
  "load /boot/bin/abi.elf"
  "load /boot/bin/seccheck.elf"
)

for cmd in "${required_cmds[@]}"; do
  if ! rg -q "\\[debug\\] script exec: ${cmd}" "$log_file"; then
    echo "runtime command was not executed: ${cmd}" >&2
    tail -n 200 "$log_file" >&2
    exit 1
  fi
done

if rg -q "failed to start program|unknown command|wait failed" "$log_file"; then
  echo "runtime command dispatch failed" >&2
  tail -n 200 "$log_file" >&2
  exit 1
fi

if rg -q "\\[debug\\] pid [0-9]+ exit=-?[1-9][0-9]*" "$log_file"; then
  echo "runtime command exited with non-zero code" >&2
  tail -n 200 "$log_file" >&2
  exit 1
fi

fail_spawn_count="$(rg -c "\\[debug\\] spawn /boot/bin/fail\\.elf argc=" "$log_file" || echo 0)"
about_spawn_count="$(rg -c "\\[debug\\] spawn /boot/bin/about\\.elf argc=" "$log_file" || echo 0)"
ok_spawn_count="$(rg -c "\\[debug\\] spawn /boot/bin/ok\\.elf argc=" "$log_file" || echo 0)"

if [[ "$fail_spawn_count" != "0" ]]; then
  echo "runtime shell exit-code control failed: fail.elf should have been skipped" >&2
  tail -n 200 "$log_file" >&2
  exit 1
fi
if [[ "$about_spawn_count" != "4" ]]; then
  echo "runtime shell exit-code control failed: about.elf spawn count is $about_spawn_count, expected 4" >&2
  tail -n 200 "$log_file" >&2
  exit 1
fi
if [[ "$ok_spawn_count" != "1" ]]; then
  echo "runtime shell exit-code control failed: ok.elf spawn count is $ok_spawn_count, expected 1" >&2
  tail -n 200 "$log_file" >&2
  exit 1
fi

if rg -q "PANIC|vector=" "$log_file"; then
  echo "panic markers found in runtime log" >&2
  tail -n 200 "$log_file" >&2
  exit 1
fi

echo "runtime tests passed"
