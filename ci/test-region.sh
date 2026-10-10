#!/usr/bin/env bash
set -euo pipefail

# Build in an isolated source copy so host objects cannot contaminate Android builds.
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_root=$(mktemp -d "${TMPDIR:-/tmp}/snes9x-region-test.XXXXXX")
trap 'rm -rf "$test_root"' EXIT
python3 - "$repo_root" "$test_root/source" <<'PY'
import shutil
import sys

shutil.copytree(sys.argv[1], sys.argv[2], ignore=shutil.ignore_patterns(
    '.git', 'build', 'artifacts', '*.o', '*.so', '*.a', '__pycache__'))
PY
make -C "$test_root/source/libretro" -j"${JOBS:-2}" LTO= >"$test_root/build.log" 2>&1 || {
    cat "$test_root/build.log"
    exit 1
}
"${CXX:-c++}" -std=c++11 -O2 -Wall -Wextra -Werror -I"$test_root/source" \
    "$test_root/source/tests/region_test.cpp" -ldl -o "$test_root/region_test"
"$test_root/region_test" "$test_root/source/libretro/snes9x_libretro.so"

"${CXX:-c++}" -std=c++11 -O2 -Wall -Wextra -Werror -I"$test_root/source" \
    "$test_root/source/tests/features_test.cpp" -ldl -o "$test_root/features_test"
(cd "$test_root" && "$test_root/features_test" "$test_root/source/libretro/snes9x_libretro.so")
