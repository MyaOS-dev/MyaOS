#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC_DIR="${ROOT_DIR}/build/toolchain/src/tinycc"
OUT_DIR="${1:-${ROOT_DIR}/build/toolchain/tcc-host}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"

for bin in git make gcc; do
  if ! command -v "${bin}" >/dev/null 2>&1; then
    echo "bootstrap_tcc_host: missing required tool: ${bin}" >&2
    exit 1
  fi
done

mkdir -p "$(dirname "${SRC_DIR}")" "${OUT_DIR}"

if [[ ! -d "${SRC_DIR}/.git" ]]; then
  git clone --depth 1 https://repo.or.cz/tinycc.git "${SRC_DIR}"
else
  git -C "${SRC_DIR}" fetch --depth 1 origin
  git -C "${SRC_DIR}" reset --hard origin/master
fi

cd "${SRC_DIR}"
./configure \
  --prefix="${OUT_DIR}" \
  --cpu=x86_64 \
  --enable-static \
  --disable-shared
make -j"${JOBS}"
make install

cat <<EOF
bootstrap_tcc_host: done
  source: ${SRC_DIR}
  install: ${OUT_DIR}
EOF
