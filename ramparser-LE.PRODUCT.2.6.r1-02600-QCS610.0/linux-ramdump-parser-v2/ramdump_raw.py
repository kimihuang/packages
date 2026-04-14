#!/usr/bin/env python3
"""Extract raw memory from QEMU ELF core dump.

Usage: python3 ramdump_raw.py [--force] [input] [output]

  input   ELF core dump file (default: binary/ramdump)
  output  Raw memory output file (default: binary/ramdump_raw.bin)
  --force Re-extract even if output is newer than input
"""

import os
import struct
import sys


def extract_raw(dump_path, raw_path, force=False):
    if not os.path.isfile(dump_path):
        print(f"Error: {dump_path} not found", file=sys.stderr)
        sys.exit(1)

    if not force and os.path.isfile(raw_path) and \
       os.path.getmtime(raw_path) >= os.path.getmtime(dump_path):
        print(f"Skip: {raw_path} is up to date")
        return

    print(f"Extracting raw memory from {dump_path} -> {raw_path}")

    os.makedirs(os.path.dirname(raw_path) or ".", exist_ok=True)

    with open(dump_path, "rb") as f:
        hdr = f.read(64)
        e_phoff = struct.unpack_from("<Q", hdr, 32)[0]
        e_phentsize = struct.unpack_from("<H", hdr, 54)[0]
        e_phnum = struct.unpack_from("<H", hdr, 56)[0]

        with open(raw_path, "wb") as out:
            for i in range(e_phnum):
                f.seek(e_phoff + i * e_phentsize)
                phdr = f.read(e_phentsize)
                p_type = struct.unpack_from("<I", phdr, 0)[0]
                if p_type != 1:  # PT_LOAD
                    continue
                p_offset = struct.unpack_from("<Q", phdr, 8)[0]
                p_filesz = struct.unpack_from("<Q", phdr, 32)[0]
                f.seek(p_offset)
                remaining = p_filesz
                while remaining > 0:
                    chunk = f.read(min(0x1000000, remaining))
                    out.write(chunk)
                    remaining -= len(chunk)

    size = os.path.getsize(raw_path)
    print(f"Done: {raw_path} ({size / (1024*1024):.1f} MB)")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "--force"]
    force = "--force" in sys.argv[1:]

    dump_path = args[0] if len(args) > 0 else "binary/ramdump"
    raw_path = args[1] if len(args) > 1 else "binary/ramdump_raw.bin"

    extract_raw(dump_path, raw_path, force)
