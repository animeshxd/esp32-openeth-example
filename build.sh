#!/bin/bash
set -e

idf.py build
# Define output merged binary
OUTPUT_BIN="build/flash_image.bin"

# Merge binaries into a single flash image
python -m esptool --chip esp32 merge_bin -o "${OUTPUT_BIN}" \
  --flash_mode dio --fill-flash-size 4MB \
  0x1000 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/main.bin

