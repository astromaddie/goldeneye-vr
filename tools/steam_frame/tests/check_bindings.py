#!/usr/bin/env python3
"""Binding table sanity (standard.md section 10): every input path the app suggests for an interaction
profile must exist on that profile, or the runtime refuses the whole profile (the Index block carried
trackpad/click, which Index lacks, and came back XR_ERROR_PATH_UNSUPPORTED). Reads
port/vr/vr_input.cpp; the component lists are the OpenXR spec's and Valve's (Steam Frame, as Halo CE VR
and F-Zero X bind it).

Run: tools/steam_frame/tests/check_bindings.py [vr_input.cpp]   (exit 1 on a bad path or a missing Frame input)
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[3] / "port/vr/vr_input.cpp"

POSES = {"grip/pose", "aim/pose"}
HAPTIC = {"output/haptic"}
STICK = {"thumbstick", "thumbstick/x", "thumbstick/y", "thumbstick/click", "thumbstick/touch"}
PAD = {"trackpad", "trackpad/x", "trackpad/y", "trackpad/touch"}

PROFILES = {
    "/interaction_profiles/khr/simple_controller": (
        {"left", "right"}, {"select/click", "menu/click"} | POSES | HAPTIC),
    "/interaction_profiles/oculus/touch_controller": (
        {"left", "right"}, {"x/click", "x/touch", "y/click", "y/touch", "menu/click", "a/click", "a/touch", "b/click",
                            "b/touch", "squeeze/value", "trigger/value", "trigger/touch", "thumbrest/touch"}
        | STICK | POSES | HAPTIC),
    "/interaction_profiles/valve/index_controller": (
        {"left", "right"}, {"system/click", "system/touch", "a/click", "a/touch", "b/click", "b/touch",
                            "squeeze/value", "squeeze/force", "trigger/click", "trigger/value", "trigger/touch",
                            "trackpad/force"} | STICK | PAD | POSES | HAPTIC),
    "/interaction_profiles/htc/vive_controller": (
        {"left", "right"}, {"system/click", "squeeze/click", "menu/click", "trigger/click", "trigger/value",
                            "trackpad/click"} | PAD | POSES | HAPTIC),
    "/interaction_profiles/microsoft/motion_controller": (
        {"left", "right"}, {"menu/click", "squeeze/click", "trigger/value", "thumbstick/click", "trackpad/click"}
        | STICK | PAD | POSES | HAPTIC),
    "/interaction_profiles/hp/mixed_reality_controller": (
        {"left", "right"}, {"x/click", "y/click", "a/click", "b/click", "menu/click", "squeeze/value", "trigger/value"}
        | STICK | POSES | HAPTIC),
    "/interaction_profiles/valve/frame_controller_valve": (
        {"left", "right"}, {"a/click", "b/click", "x/click", "y/click", "menu/click", "view/click", "bumper/click",
                            "trigger/value", "trigger/click", "squeeze/value", "dpad_up/click", "dpad_down/click",
                            "dpad_left/click", "dpad_right/click"} | STICK | POSES | HAPTIC),
}
# Which hand has which button on the Frame (a split gamepad): this is what the game's mapping relies on.
FRAME_HAND = {"a/click": "right", "b/click": "right", "x/click": "right", "y/click": "right", "menu/click": "right",
              "view/click": "left", "dpad_up/click": "left", "dpad_down/click": "left", "dpad_left/click": "left",
              "dpad_right/click": "left"}
# Needed by the game: these actions must be bound on the Frame profile.
FRAME_NEEDS = ["gPoseAction", "gSelectAction", "gTriggerValueAction", "gGripAction", "gGripValueAction",
               "gButtonAAction", "gButtonBAction", "gButtonXAction", "gButtonYAction", "gMenuAction", "gViewAction",
               "gThumbstickAction", "gThumbstickClickAction", "gHapticAction"]


def blocks(text):
    for m in re.finditer(r'SuggestBindings\("([^"]+)",\s*\{(.*?)\n\s*\}\);', text, re.S):
        yield m.group(1), m.group(2)


def main():
    text = (Path(sys.argv[1]) if len(sys.argv) > 1 else SRC).read_text()
    bad = 0
    seen = set()
    for profile, body in blocks(text):
        seen.add(profile)
        hands, comps = PROFILES.get(profile, (None, None))
        actions = set()
        if comps is None:
            print(f"note: {profile}: no component list here (needs an extension on SteamVR), skipped")
            continue
        for line in body.splitlines():
            line = line.split("//")[0]
            m = re.search(r'\{(g\w+(?:\[\d\])?),\s*"(/user/hand/(\w+)/(input|output)/([\w/]+))"\}', line)
            if not m:
                continue
            action, _path, hand, kind, comp = m.groups()
            comp = comp if kind == "input" else "output/" + comp
            actions.add(action)
            if hand not in hands or comp not in comps:
                print(f"BAD  {profile}: {_path} is not a component of that profile")
                bad += 1
            if profile.endswith("frame_controller_valve") and comp in FRAME_HAND and FRAME_HAND[comp] != hand:
                print(f"BAD  {profile}: {_path}: the Frame has {comp} on the {FRAME_HAND[comp]} controller")
                bad += 1
        if profile.endswith("frame_controller_valve"):
            for need in FRAME_NEEDS:
                if need not in actions:
                    print(f"BAD  Frame profile does not bind {need}")
                    bad += 1
    if "/interaction_profiles/valve/frame_controller_valve" not in seen:
        print("BAD  no Steam Frame profile block in vr_input.cpp")
        bad += 1
    print(f"checked {len(seen)} profiles: {'FAILED' if bad else 'ok'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
