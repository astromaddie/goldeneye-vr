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

# the gun's aim on the Frame (port/vr/vr_aim_math.c), the production functions
"$cc" -std=c11 -Wall -Wextra -Werror -I"$root/port/vr" \
    "$here/test_vr_aim.c" "$root/port/vr/vr_aim_math.c" -lm -o "$out/test_vr_aim"
"$out/test_vr_aim"

# the paused watch's screen placement (port/vr/vr_screen_math.c), the production functions
"$cc" -std=c11 -Wall -Wextra -Werror -I"$root/port/vr" \
    "$here/test_vr_screen.c" "$root/port/vr/vr_screen_math.c" -lm -o "$out/test_vr_screen"
"$out/test_vr_screen"

# the 3D cinema for cutscenes (port/vr/vr_cinema_math.c): placement, the per-eye projection views
# through the screen's corners, the stereo shear against two real off-axis cameras, fade, latch
"$cc" -std=c11 -Wall -Wextra -Werror -I"$root/port/vr" \
    "$here/test_vr_cinema.c" "$root/port/vr/vr_cinema_math.c" "$root/port/vr/vr_screen_math.c" -lm -o "$out/test_vr_cinema"
"$out/test_vr_cinema"

# upright quads (port/vr/vr_upright_math.c): billboards, the 3D sight and the HUD quads keep world up
"$cc" -std=c11 -Wall -Wextra -Werror -I"$root/port/vr" \
    "$here/test_vr_upright.c" "$root/port/vr/vr_upright_math.c" -lm -o "$out/test_vr_upright"
"$out/test_vr_upright"

# the scope lens (port/vr/vr_scope_math.c): the picture on the eyepiece's disc from each eye, the scope
# camera on the shot ray, against a reference pinhole and look-at camera; the 4811c1e optics bug reproduced
"$cc" -std=c11 -Wall -Wextra -Werror -I"$root/port/vr" \
    "$here/test_vr_scope.c" "$root/port/vr/vr_scope_math.c" -lm -o "$out/test_vr_scope"
"$out/test_vr_scope"

# the menu pointer's quads (port/vr/vr_pointer_math.c): beam and dot always in front of the panel
"$cc" -std=c11 -Wall -Wextra -Werror -I"$root/port/vr" \
    "$here/test_vr_pointer.c" "$root/port/vr/vr_pointer_math.c" -lm -o "$out/test_vr_pointer"
"$out/test_vr_pointer"

# the display rate (port/vr/vr_refresh_math.c): one request only if offered, the per-app rate recommended after two low worn windows
"$cc" -std=c11 -Wall -Wextra -Werror -I"$root/port/vr" \
    "$here/test_vr_refresh.c" "$root/port/vr/vr_refresh_math.c" -o "$out/test_vr_refresh"
"$out/test_vr_refresh"

# goldeneye-vr.ini: every key the in-game VR settings change, save -> load (port/vr/vr_settings.cpp,
# vr_settings_defaults.c, vr_haptics.cpp, vr_log.c: the production files)
"$cc" -std=c11 -Wall -Wextra -DGEVR_STEAM_FRAME=1 -I"$root/include" -I"$root/port/include" \
    -c "$root/port/vr/vr_settings_defaults.c" -o "$out/vr_settings_defaults.o"
"$cc" -std=c11 -Wall -Wextra -c "$root/port/vr/vr_log.c" -o "$out/vr_log.o"
"$cc" -std=c11 -Wall -Wextra -c "$root/port/vr/vr_refresh_math.c" -o "$out/vr_refresh_math.o"
"$cxx" -std=c++17 -Wall -Wextra -DGEVR_STEAM_FRAME=1 -I"$root/port/vr" -I"$root/OpenXR/Include" -I"$here/fake" \
    "$here/test_vr_settings.cpp" "$root/port/vr/vr_settings.cpp" "$root/port/vr/vr_haptics.cpp" \
    "$out/vr_settings_defaults.o" "$out/vr_log.o" "$out/vr_refresh_math.o" -o "$out/test_vr_settings"
"$out/test_vr_settings"
