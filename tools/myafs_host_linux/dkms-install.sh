#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_NAME="myafs-host"
VERSION="$(tr -d ' \t\r\n' < "${SCRIPT_DIR}/VERSION")"
SRC_DIR="/usr/src/${PACKAGE_NAME}-${VERSION}"
KERNEL_VER="$(uname -r)"
KERNEL_SOURCE_DIR=""

usage() {
    cat <<'EOF'
usage: dkms-install.sh [--kernelver <ver>] [--kernelsourcedir <path>]

Options:
  --kernelver <ver>         Kernel version to build/install for (default: uname -r)
  --kernelsourcedir <path>  Override kernel source/build dir for DKMS build
  -h, --help                Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --kernelver)
            if [[ $# -lt 2 ]]; then
                echo "error: --kernelver requires a value"
                exit 2
            fi
            KERNEL_VER="$2"
            shift 2
            ;;
        --kernelsourcedir)
            if [[ $# -lt 2 ]]; then
                echo "error: --kernelsourcedir requires a value"
                exit 2
            fi
            KERNEL_SOURCE_DIR="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown argument: $1"
            usage
            exit 2
            ;;
    esac
done

if [[ "${EUID}" -ne 0 ]]; then
    echo "error: run as root (use sudo)"
    exit 1
fi

if ! command -v dkms >/dev/null 2>&1; then
    echo "error: dkms is not installed"
    exit 1
fi

DEFAULT_BUILD_DIR="/usr/lib/modules/${KERNEL_VER}/build"
if [[ -z "${KERNEL_SOURCE_DIR}" && ! -d "${DEFAULT_BUILD_DIR}" ]]; then
    echo "error: kernel headers for ${KERNEL_VER} are missing."
    echo "expected: ${DEFAULT_BUILD_DIR}"
    if command -v pacman >/dev/null 2>&1; then
        echo "fix (Arch): sudo pacman -S --needed linux-headers"
    fi
    echo "or rerun with: --kernelsourcedir /path/to/kernel/build"
    exit 21
fi

if [[ -n "${KERNEL_SOURCE_DIR}" && ! -d "${KERNEL_SOURCE_DIR}" ]]; then
    echo "error: kernelsourcedir does not exist: ${KERNEL_SOURCE_DIR}"
    exit 21
fi

echo "[1/5] Preparing source at ${SRC_DIR}"
rm -rf "${SRC_DIR}"
install -d "${SRC_DIR}"
install -m 0644 "${SCRIPT_DIR}/myafs_linux.c" "${SRC_DIR}/myafs_linux.c"
install -m 0644 "${SCRIPT_DIR}/Makefile" "${SRC_DIR}/Makefile"
install -m 0644 "${SCRIPT_DIR}/dkms.conf" "${SRC_DIR}/dkms.conf"
install -m 0644 "${SCRIPT_DIR}/VERSION" "${SRC_DIR}/VERSION"

if dkms status -m "${PACKAGE_NAME}" -v "${VERSION}" 2>/dev/null | grep -q .; then
    echo "[2/5] Removing existing DKMS registration for ${PACKAGE_NAME}/${VERSION}"
    dkms remove -m "${PACKAGE_NAME}" -v "${VERSION}" --all || true
else
    echo "[2/5] No existing DKMS registration for ${PACKAGE_NAME}/${VERSION}"
fi

echo "[3/5] Adding module to DKMS"
dkms add -m "${PACKAGE_NAME}" -v "${VERSION}"

echo "[4/5] Building module with DKMS"
build_cmd=(dkms build -m "${PACKAGE_NAME}" -v "${VERSION}" -k "${KERNEL_VER}")
if [[ -n "${KERNEL_SOURCE_DIR}" ]]; then
    build_cmd+=(--kernelsourcedir "${KERNEL_SOURCE_DIR}")
fi
"${build_cmd[@]}"

echo "[5/5] Installing module with DKMS"
dkms install -m "${PACKAGE_NAME}" -v "${VERSION}" -k "${KERNEL_VER}"

echo
echo "Installed DKMS package ${PACKAGE_NAME}/${VERSION}."
echo "Module name: myafs_linux"
echo "Load now with: modprobe myafs_linux"
