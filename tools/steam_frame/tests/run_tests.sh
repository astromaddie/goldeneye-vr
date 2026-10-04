#!/usr/bin/env bash
# Host tests for the Steam Frame code that needs no headset (evidence level 1, verification.md).
#   tools/steam_frame/tests/run_tests.sh
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

python3 "$here/check_bindings.py"

cxx="${CXX:-clang++}"
cc="${CC:-clang}"
for f in "$root"/third_party/sfvr/src/sfvr_view.c; do
    "$cc" -std=c11 -Wall -Wextra -c "$f" -I"$root/third_party/sfvr/include" -o "$out/$(basename "$f").o"
done
"$cxx" -std=c++17 -Wall -Wextra -DGEVR_STEAM_FRAME=1 \
    -I"$here/fake" -I"$root/OpenXR/Include" -I"$root/third_party/sfvr/include" \
    "$here/test_vr_system.cpp" "$root/port/vr/vr_system.cpp" "$out"/sfvr_view.c.o -o "$out/test_vr_system"
"$out/test_vr_system"

# comfort locomotion's math (port/vr/vr_motion_math.c), the production functions
"$cc" -std=c11 -Wall -Wextra -Werror -I"$root/port/vr" \
    "$here/test_vr_motion.c" "$root/port/vr/vr_motion_math.c" -lm -o "$out/test_vr_motion"
"$out/test_vr_motion"
