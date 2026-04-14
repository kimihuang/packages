#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

RAW_FILE="binary/ramdump_raw.bin"
OUTPUT_DIR="output"

python3 ramdump_raw.py binary/ramdump "$RAW_FILE"

mkdir -p "$OUTPUT_DIR"

python3 ramparse.py \
    --ram-file "$RAW_FILE" 0x40000000 0x80000000 \
    --vmlinux binary/vmlinux \
    --force-hardware QEMU_VIRT \
    --dmesg --print-tasks --print-irqs --print-runqueues \
    --sched-info --check-for-panic --cpu-state \
    --print-memory-info --slabinfo --print-vmalloc \
    --dump-page-tables \
    -o "$OUTPUT_DIR"
