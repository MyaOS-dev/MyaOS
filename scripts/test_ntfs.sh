#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

OVMF_CODE="${OVMF_CODE:-/usr/share/edk2/x64/OVMF_CODE.4m.fd}"
OVMF_VARS_TEMPLATE="${OVMF_VARS_TEMPLATE:-/usr/share/edk2/x64/OVMF_VARS.4m.fd}"

if [[ ! -f "$OVMF_CODE" ]]; then
  echo "missing OVMF code file: $OVMF_CODE" >&2
  exit 1
fi
if [[ ! -f "$OVMF_VARS_TEMPLATE" ]]; then
  echo "missing OVMF vars template: $OVMF_VARS_TEMPLATE" >&2
  exit 1
fi

make clean >/dev/null
make -j4 MYAOS_DEBUG=1 build/esp.img >/dev/null

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

ntfs_img="$tmp_dir/ntfs.img"
vars_file="$tmp_dir/OVMF_VARS.fd"
autorun_file="$tmp_dir/autorun.sh"
log_file="$tmp_dir/qemu-ntfs.log"
harness_src="$tmp_dir/ntfs_harness.c"
harness_bin="$tmp_dir/ntfs_harness"

truncate -s 16M "$ntfs_img"
mkntfs -F -Q -L TEST "$ntfs_img" >/dev/null
printf 'hello-from-ntfs\n' > "$tmp_dir/hello.txt"
ntfscp -q "$ntfs_img" "$tmp_dir/hello.txt" /hello.txt
for i in $(seq -w 0 95); do
  printf 'entry-%s\n' "$i" > "$tmp_dir/file${i}.txt"
  ntfscp -q "$ntfs_img" "$tmp_dir/file${i}.txt" "/file${i}.txt"
done
python3 - "$tmp_dir/big.bin" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
size = 98304
data = bytearray(size)
for i in range(size):
    data[i] = (i * 7 + 3) & 0xFF
path.write_bytes(data)
PY
ntfscp -q "$ntfs_img" "$tmp_dir/big.bin" /big.bin

cat >"$harness_src" <<'EOF'
#include "kernel/fs/ntfs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int load_image(const char* path, uint8_t** out_buf, size_t* out_size) {
    FILE* f;
    long size;
    uint8_t* buf;

    if (!path || !out_buf || !out_size) {
        return -1;
    }

    f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }

    buf = (uint8_t*)malloc((size_t)size);
    if (!buf) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);

    *out_buf = buf;
    *out_size = (size_t)size;
    return 0;
}

static uint8_t expected_byte(uint32_t index) {
    return (uint8_t)((index * 7u + 3u) & 0xFFu);
}

static uint8_t updated_byte(uint32_t index) {
    return (uint8_t)((255u - ((index * 5u + 11u) & 0xFFu)) & 0xFFu);
}

int main(int argc, char** argv) {
    uint8_t* image = NULL;
    size_t image_size = 0u;
    ntfs_fs_t fs;
    ntfs_dirent_t entries[160];
    ntfs_dirent_t hello;
    ntfs_dirent_t big;
    uint8_t hello_buf[32];
    uint8_t* file_buf = NULL;
    uint8_t* write_buf = NULL;
    uint32_t list_count = 0u;
    uint32_t read_size = 0u;
    static const uint8_t resident_text[] = "resident-write!!";

    if (argc != 3) {
        return 2;
    }
    if (load_image(argv[1], &image, &image_size) != 0) {
        return 3;
    }
    if (ntfs_mount(&fs, image, (uint64_t)image_size) != 0) {
        free(image);
        return 4;
    }
    if (ntfs_list_dir(&fs, NTFS_ROOT_RECORD, entries, 160u, &list_count) != 0 || list_count < 98u) {
        free(image);
        return 5;
    }
    if (ntfs_lookup(&fs, NTFS_ROOT_RECORD, "hello.txt", &hello) != 0 || hello.is_dir || hello.size != 16u) {
        free(image);
        return 6;
    }
    if (ntfs_lookup(&fs, NTFS_ROOT_RECORD, "big.bin", &big) != 0 || big.is_dir || big.size != 98304u) {
        free(image);
        free(image);
        return 7;
    }
    if (ntfs_read_file(&fs, big.record_no, NULL, 0u, &read_size) != 0 || read_size != (uint32_t)big.size) {
        free(image);
        return 8;
    }

    file_buf = (uint8_t*)malloc((size_t)read_size);
    if (!file_buf) {
        free(image);
        return 10;
    }
    if (ntfs_read_file(&fs, big.record_no, file_buf, read_size, &read_size) != 0 || read_size != (uint32_t)big.size) {
        free(file_buf);
        free(image);
        return 11;
    }

    for (uint32_t i = 0u; i < read_size; i += 4096u) {
        if (file_buf[i] != expected_byte(i)) {
            free(file_buf);
            free(image);
            return 12;
        }
    }
    if (file_buf[read_size - 1u] != expected_byte(read_size - 1u)) {
        free(file_buf);
        free(image);
        return 13;
    }

    if (ntfs_write_file(&fs, hello.record_no, resident_text, sizeof(resident_text) - 1u) != 0) {
        free(file_buf);
        free(image);
        return 14;
    }
    if (ntfs_write_file(&fs, hello.record_no, resident_text, sizeof(resident_text) - 2u) == 0) {
        free(file_buf);
        free(image);
        return 15;
    }
    if (ntfs_read_file(&fs, hello.record_no, hello_buf, sizeof(hello_buf), &read_size) != 0 ||
        read_size != sizeof(resident_text) - 1u ||
        memcmp(hello_buf, resident_text, sizeof(resident_text) - 1u) != 0) {
        free(file_buf);
        free(image);
        return 16;
    }

    write_buf = (uint8_t*)malloc((size_t)big.size);
    if (!write_buf) {
        free(file_buf);
        free(image);
        return 17;
    }
    for (uint32_t i = 0u; i < (uint32_t)big.size; i++) {
        write_buf[i] = updated_byte(i);
    }
    if (ntfs_write_file(&fs, big.record_no, write_buf, (uint32_t)big.size) != 0) {
        free(write_buf);
        free(file_buf);
        free(image);
        return 18;
    }
    if (ntfs_write_file(&fs, big.record_no, write_buf, (uint32_t)big.size - 1u) == 0) {
        free(write_buf);
        free(file_buf);
        free(image);
        return 19;
    }
    if (ntfs_read_file(&fs, big.record_no, file_buf, (uint32_t)big.size, &read_size) != 0 ||
        read_size != (uint32_t)big.size) {
        free(write_buf);
        free(file_buf);
        free(image);
        return 20;
    }
    for (uint32_t i = 0u; i < read_size; i += 4096u) {
        if (file_buf[i] != updated_byte(i)) {
            free(write_buf);
            free(file_buf);
            free(image);
            return 21;
        }
    }
    if (file_buf[read_size - 1u] != updated_byte(read_size - 1u)) {
        free(write_buf);
        free(file_buf);
        free(image);
        return 22;
    }

    free(write_buf);
    free(file_buf);
    free(image);
    return 0;
}
EOF

${CC:-cc} -std=c11 -Wall -Wextra -I"$ROOT_DIR/include" -I"$ROOT_DIR" \
  "$harness_src" "$ROOT_DIR/kernel/fs/ntfs.c" -o "$harness_bin"
"$harness_bin" "$ntfs_img" selftest

cat >"$autorun_file" <<'EOF'
debug on
attach disk1 /ntfs
attached
save /ntfs/hello.txt abcdefghijklmnop
show /ntfs/hello.txt
poweroff
EOF

cp "$OVMF_VARS_TEMPLATE" "$vars_file"
mcopy -o -i build/esp.img "$autorun_file" ::/autorun.sh

qemu_rc=0
if timeout 60s qemu-system-x86_64 \
  -machine q35 \
  -m 1G \
  -display none \
  -serial none \
  -debugcon "file:$log_file" \
  -global isa-debugcon.iobase=0xe9 \
  -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
  -drive if=pflash,format=raw,file="$vars_file" \
  -drive file=build/esp.img,format=raw \
  -drive file="$ntfs_img",format=raw; then
  qemu_rc=0
else
  qemu_rc=$?
fi

if [[ "$qemu_rc" -ne 0 && "$qemu_rc" -ne 124 ]]; then
  echo "qemu ntfs test failed with rc=$qemu_rc" >&2
  [[ -f "$log_file" ]] && strings "$log_file" | tail -n 200 >&2
  exit 1
fi

if [[ ! -f "$log_file" ]]; then
  echo "ntfs test log was not produced" >&2
  exit 1
fi

if ! rg -a -q "\\[debug\\] spawn /bin/attach\\.elf argc=3" "$log_file"; then
  echo "ntfs mount test: attach command was not executed" >&2
  strings "$log_file" | tail -n 200 >&2
  exit 1
fi

if ! rg -a -q "/ntfs -> ntfs from disk1" "$log_file"; then
  echo "ntfs mount test: mount does not appear in attached output" >&2
  strings "$log_file" | tail -n 200 >&2
  exit 1
fi

if rg -a -q "/ntfs -> ntfs from disk1 ro" "$log_file"; then
  echo "ntfs mount test: writable disk was mounted read-only" >&2
  strings "$log_file" | tail -n 200 >&2
  exit 1
fi

if rg -a -q "PANIC|vector=" "$log_file"; then
  echo "ntfs mount test: fatal markers found" >&2
  strings "$log_file" | tail -n 200 >&2
  exit 1
fi

if ! rg -a -q "^abcdefghijklmnop$" "$log_file"; then
  echo "ntfs mount test: updated file contents were not visible in guest" >&2
  strings "$log_file" | tail -n 200 >&2
  exit 1
fi

echo "ntfs tests passed"
