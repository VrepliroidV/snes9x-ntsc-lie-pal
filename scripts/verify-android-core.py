#!/usr/bin/env python3
"""Validate the Android ARM64 ELF before distributing it to RetroArch.

This is a static compatibility check, not an Android dlopen or gameplay test.
The NDK API-21 linker resolves imports against the Android system stubs.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys


REQUIRED_EXPORTS = {
    "retro_init", "retro_deinit", "retro_api_version", "retro_get_system_info",
    "retro_get_system_av_info", "retro_set_environment", "retro_set_video_refresh",
    "retro_set_audio_sample", "retro_set_audio_sample_batch", "retro_set_input_poll",
    "retro_set_input_state", "retro_set_controller_port_device", "retro_reset",
    "retro_run", "retro_serialize_size", "retro_serialize", "retro_unserialize",
    "retro_cheat_reset", "retro_cheat_set", "retro_load_game", "retro_load_game_special",
    "retro_unload_game", "retro_get_region", "retro_get_memory_data", "retro_get_memory_size",
}
ANDROID_SYSTEM_LIBRARIES = {"libc.so", "libm.so", "libdl.so", "liblog.so", "libz.so"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate(core, readelf, android_stubs_dir):
    data = core.read_bytes()
    require(len(data) >= 64, "File is too short to be an ELF64 shared object")
    require(data[:7] == b"\x7fELF\x02\x01\x01", "Expected little-endian ELF64")
    header = struct.unpack_from("<HHIQQQIHHHHHH", data, 16)
    elf_type, machine = header[:2]
    require(elf_type == 3, "Expected ET_DYN (shared object)")
    require(machine == 183, "Expected AArch64 (Android arm64-v8a), not a host build")
    phoff, phentsize, phnum = header[4], header[8], header[9]
    require(phentsize >= 56, "Invalid ELF64 program-header size")
    require(phoff + phentsize * phnum <= len(data), "Truncated ELF program headers")

    load_alignments = []
    stack_is_non_executable = False
    android_api = None
    ndk_version = None
    for index in range(phnum):
        kind, flags, offset, vaddr, _, filesz, _, alignment = struct.unpack_from(
            "<IIQQQQQQ", data, phoff + index * phentsize
        )
        if kind == 1:  # PT_LOAD
            require(offset + filesz <= len(data), "Truncated ELF LOAD segment")
            require(alignment >= 16384 and alignment & (alignment - 1) == 0,
                    "LOAD alignment must be a power of two >= 16384 bytes for Android 16KB pages")
            require(offset % 16384 == vaddr % 16384,
                    "LOAD file offset and virtual address must agree modulo 16384")
            load_alignments.append(alignment)
        if kind == 0x6474E551:  # PT_GNU_STACK
            require(not flags & 1, "ELF requests an executable stack")
            stack_is_non_executable = True
        if kind == 4:  # PT_NOTE, including .note.android.ident
            cursor, end = offset, offset + filesz
            require(end <= len(data), "Truncated ELF NOTE segment")
            while cursor + 12 <= end:
                namesz, descsz, note_type = struct.unpack_from("<III", data, cursor)
                cursor += 12
                owner = data[cursor:cursor + namesz].rstrip(b"\0")
                cursor += (namesz + 3) & ~3
                require(cursor + descsz <= end, "Truncated ELF NOTE payload")
                description = data[cursor:cursor + descsz]
                cursor += (descsz + 3) & ~3
                if owner == b"Android" and note_type == 1 and descsz >= 4:
                    android_api = struct.unpack_from("<I", description)[0]
                    if descsz >= 68:
                        ndk_version = description[4:68].split(b"\0", 1)[0].decode("ascii")
    require(load_alignments, "ELF has no loadable segments")
    require(stack_is_non_executable, "ELF lacks a non-executable GNU_STACK header")
    require(android_api == 21, "Expected Android API-21 build metadata in .note.android.ident")

    elf_info = subprocess.check_output(
        [readelf, "--wide", "--dynamic", "--dyn-syms", str(core)], text=True
    )
    dependencies = sorted(re.findall(r"\(NEEDED\).*\[([^\]]+)\]", elf_info))
    require(set(dependencies) <= ANDROID_SYSTEM_LIBRARIES,
            "Unexpected Android runtime dependencies: " +
            ", ".join(sorted(set(dependencies) - ANDROID_SYSTEM_LIBRARIES)))
    require(not re.search(r"\((TEXTREL|RPATH|RUNPATH)\)", elf_info),
            "ELF has text relocations or a runtime library search path")

    exports = set()
    function_exports = set()
    imports = set()
    strong_imports = set()
    for line in elf_info.splitlines():
        fields = line.split()
        if len(fields) >= 8 and re.fullmatch(r"\d+:", fields[0]):
            name = fields[7].split("@", 1)[0]
            if fields[6] == "UND":
                if name:
                    imports.add(name)
                    if fields[4] != "WEAK":
                        strong_imports.add(name)
            elif fields[4] in {"GLOBAL", "WEAK"} and fields[5] == "DEFAULT":
                exports.add(name)
                if fields[3] == "FUNC":
                    function_exports.add(name)
    require(REQUIRED_EXPORTS <= function_exports,
            "Missing Libretro function entry points: " +
            ", ".join(sorted(REQUIRED_EXPORTS - function_exports)))
    require(all(symbol.startswith("retro_") for symbol in exports),
            "Non-Libretro symbols escaped the core's visibility/version script")

    if android_stubs_dir:
        available_symbols = set()
        for dependency in dependencies:
            stub = android_stubs_dir / dependency
            require(stub.is_file(), f"Android API-21 stub not found: {stub}")
            stub_info = subprocess.check_output(
                [readelf, "--wide", "--dyn-syms", str(stub)], text=True
            )
            for line in stub_info.splitlines():
                fields = line.split()
                if (len(fields) >= 8 and re.fullmatch(r"\d+:", fields[0])
                        and fields[6] != "UND" and fields[4] in {"GLOBAL", "WEAK"}):
                    available_symbols.add(fields[7].split("@", 1)[0])
        require(strong_imports <= available_symbols,
                "Strong imports absent from the Android API-21 system stubs: " +
                ", ".join(sorted(strong_imports - available_symbols)))

    return {
        "status": "passed",
        "file": core.name,
        "sha256": hashlib.sha256(data).hexdigest(),
        "size_bytes": len(data),
        "elf_class": "ELF64",
        "machine": "AArch64",
        "elf_type": "ET_DYN",
        "verified_android_api": 21 if android_stubs_dir else None,
        "android_api_build_note": android_api,
        "ndk_version_build_note": ndk_version,
        "load_segment_alignments": load_alignments,
        "non_executable_stack": True,
        "needed_libraries": dependencies,
        "libretro_exports": sorted(exports),
        "imported_symbols": sorted(imports),
        "weak_imported_symbols": sorted(imports - strong_imports),
        "limitations": "Static ELF validation; Android dlopen, RetroArch and device gameplay still require testing.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("core", type=Path)
    parser.add_argument("--readelf", default="llvm-readelf")
    parser.add_argument("--android-stubs-dir", type=Path,
                        help="NDK sysroot/usr/lib/aarch64-linux-android/21 directory")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        report = validate(args.core, args.readelf, args.android_stubs_dir)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Android ELF validation failed: {error}", file=sys.stderr)
        return 1
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(f"ELF validation passed: {report['machine']}, 16KB LOAD alignment, "
          f"{len(report['libretro_exports'])} Libretro exports, dependencies "
          f"{', '.join(report['needed_libraries']) or '(none)'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
