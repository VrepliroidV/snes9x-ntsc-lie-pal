#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ndk_dir="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
output_dir="${1:-${repo_dir}/build/android-arm64}"
jobs="${JOBS:-$(getconf _NPROCESSORS_ONLN)}"
rebuild_flags=()
if [[ "${FORCE_REBUILD:-0}" == 1 ]]; then
    rebuild_flags=(-B)
fi

if [[ -z "$ndk_dir" || ! -x "$ndk_dir/ndk-build" ]]; then
    echo 'Set ANDROID_NDK_HOME to an installed Android NDK (CI uses r28c / 28.2.13676358).' >&2
    exit 1
fi

mkdir -p -- "$output_dir"
output_dir="$(cd -- "$output_dir" && pwd)"
cd -- "$repo_dir"

"$ndk_dir/ndk-build" \
    NDK_PROJECT_PATH="$repo_dir/libretro" \
    APP_BUILD_SCRIPT="$repo_dir/libretro/jni/Android.mk" \
    NDK_APPLICATION_MK="$repo_dir/libretro/jni/Application.mk" \
    NDK_OUT="$output_dir/obj" \
    NDK_LIBS_OUT="$output_dir/libs" \
    APP_ABI=arm64-v8a \
    APP_PLATFORM=android-21 \
    APP_SUPPORT_FLEXIBLE_PAGE_SIZES=true \
    APP_LDFLAGS=-Wl,--no-undefined \
    APP_OPTIM=release \
    NDK_DEBUG=0 \
    "${rebuild_flags[@]}" \
    -j"$jobs" 2>&1 | tee "$output_dir/build.log"

core_name=snes9x_libretro_android.so
cp -- "$output_dir/libs/arm64-v8a/libretro.so" "$output_dir/$core_name"
cp -- "$repo_dir/LICENSE" "$output_dir/LICENSE"
cp -- "$repo_dir/filter/snes_ntsc-license.txt" "$output_dir/snes_ntsc-license.txt"
cp -- "$ndk_dir/NOTICE" "$output_dir/NDK-NOTICE.txt"
cp -- "$ndk_dir/NOTICE.toolchain" "$output_dir/NDK-NOTICE.toolchain.txt"
llvm_dir="$ndk_dir/toolchains/llvm/prebuilt/linux-x86_64"
readelf_path="$llvm_dir/bin/llvm-readelf"
if [[ ! -x "$readelf_path" ]]; then
    echo "NDK LLVM readelf was not found: $readelf_path (this script targets a Linux build host)." >&2
    exit 1
fi

python3 "$repo_dir/scripts/verify-android-core.py" \
    "$output_dir/$core_name" \
    --readelf "$readelf_path" \
    --android-stubs-dir "$llvm_dir/sysroot/usr/lib/aarch64-linux-android/21" \
    --report "$output_dir/elf-validation.json"

"$llvm_dir/bin/aarch64-linux-android21-clang" \
    -O2 -Wall -Wextra -Werror -Wl,-z,max-page-size=16384 \
    -I"$repo_dir/libretro" "$repo_dir/scripts/android-core-smoke.c" \
    -ldl -o "$output_dir/android-core-smoke"

python3 - "$output_dir/build.log" "$output_dir/build-diagnostics.json" <<'PY'
import json
from pathlib import Path
import re
import sys

lines = Path(sys.argv[1]).read_text().splitlines()
warnings = [line for line in lines if re.search(r"\bwarning:", line)]
errors = [line for line in lines if re.search(r"\berror:", line)]
Path(sys.argv[2]).write_text(json.dumps({
    "warning_count": len(warnings), "error_count": len(errors),
    "warnings": warnings, "errors": errors,
}, indent=2) + "\n")
PY

{
    git rev-parse HEAD
    git status --short
    cat "$ndk_dir/source.properties"
    printf 'APP_ABI=arm64-v8a\nAPP_PLATFORM=android-21\nAPP_SUPPORT_FLEXIBLE_PAGE_SIZES=true\nAPP_STL=c++_static\nAPP_LDFLAGS=-Wl,--no-undefined\n'
} > "$output_dir/build-metadata.txt"
(cd -- "$output_dir" && sha256sum "$core_name" android-core-smoke > SHA256SUMS)
printf 'Validated Android ARM64 core: %s/%s\n' "$output_dir" "$core_name"
