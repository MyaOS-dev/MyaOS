#!/usr/bin/env sh
set -eu

cc -std=c11 -O2 -Wall -Wextra \
  tools/myafs_driver/myafs_driver.c \
  tools/myafs_driver/main.c \
  -o tools/myafs_driver/myafsdrv

cc -std=c11 -O2 -Wall -Wextra \
  tools/myafs_driver/myafs_driver.c \
  tools/myafs_driver/mkfs_myafs.c \
  -o tools/myafs_driver/mkfs.myafs

echo "built tools/myafs_driver/myafsdrv"
echo "built tools/myafs_driver/mkfs.myafs"
