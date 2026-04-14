#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

make -j4 >/dev/null

required_files=(
  "build/BOOTX64.EFI"
  "build/kernel.elf"
  "build/esp.img"
  "build/programs/system/msh.elf"
  "build/programs/system/syscfg.elf"
  "build/programs/system/whoami.elf"
  "build/programs/system/login.elf"
  "build/programs/system/abi.elf"
  "build/programs/system/pkg.elf"
  "build/programs/system/update.elf"
  "build/programs/system/modctl.elf"
  "build/programs/system/hotplug.elf"
  "build/programs/system/compat.elf"
  "build/programs/system/uname.elf"
  "build/programs/system/kill.elf"
  "build/programs/debug/cpuburn.elf"
  "build/programs/debug/limitcheck.elf"
  "build/programs/debug/notifycheck.elf"
  "build/programs/debug/schedcheck.elf"
  "build/programs/debug/tasks.elf"
  "build/programs/debug/fbinfo.elf"
  "build/programs/debug/netstat.elf"
  "build/programs/debug/swapstat.elf"
  "build/programs/debug/swapcheck.elf"
  "build/programs/debug/posixcheck.elf"
  "build/programs/debug/seccheck.elf"
  "build/programs/debug/dlcheck.elf"
  "build/programs/debug/p0check.elf"
  "build/lib/libdemo.so"
  "build/lib/liblinkdemo.so"
  "build/programs/ipc/pipecheck.elf"
  "build/programs/ipc/shmcheck.elf"
  "build/programs/ipc/sockcheck.elf"
  "build/programs/ipc/threadcheck.elf"
  "build/programs/ipc/notifywait.elf"
  "build/programs/net/netsend.elf"
  "build/programs/net/netrecv.elf"
)

for path in "${required_files[@]}"; do
  if [[ ! -f "$path" ]]; then
    echo "missing required artifact: $path" >&2
    exit 1
  fi
done

echo "smoke tests passed"
