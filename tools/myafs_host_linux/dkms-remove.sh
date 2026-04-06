#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_NAME="myafs-host"
DEFAULT_VERSION="$(tr -d ' \t\r\n' < "${SCRIPT_DIR}/VERSION")"
VERSION="${1:-${DEFAULT_VERSION}}"
SRC_DIR="/usr/src/${PACKAGE_NAME}-${VERSION}"

if [[ "${EUID}" -ne 0 ]]; then
    echo "error: run as root (use sudo)"
    exit 1
fi

if ! command -v dkms >/dev/null 2>&1; then
    echo "error: dkms is not installed"
    exit 1
fi

if dkms status -m "${PACKAGE_NAME}" -v "${VERSION}" 2>/dev/null | grep -q .; then
    echo "Removing DKMS package ${PACKAGE_NAME}/${VERSION}"
    dkms remove -m "${PACKAGE_NAME}" -v "${VERSION}" --all
else
    echo "DKMS package ${PACKAGE_NAME}/${VERSION} is not registered"
fi

if [[ -d "${SRC_DIR}" ]]; then
    echo "Removing source directory ${SRC_DIR}"
    rm -rf "${SRC_DIR}"
fi

echo "Done."

