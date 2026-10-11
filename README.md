# GoldenEye VR for Steam Frame — experimental technical preview

This private fork adds a native Steam Frame target to GoldenEye VR: Lepton (Android 11/API 30), arm64-v8a, GLES 3.2, SteamVR OpenXR, and multiview rendering. It is an experimental port preview and makes no complete-gameplay claim.

The upstream project is [MrSco/goldeneye-vr](https://github.com/MrSco/goldeneye-vr). General GoldenEye VR fixes belong upstream; Frame-specific work belongs in this fork and needs owner access. The Quest-oriented README is preserved in [README_QUEST.md](README_QUEST.md). This repository remains private.

## Capability evidence

| Capability | Host | Frame on desk | Worn |
|---|---|---|---|
| Build and OpenXR | Host test suite passed at source 61fb1cf; no PR0 APK build was run. | Earlier installs reached SteamVR OpenXR with multiview and booted selected scenes. | Current-head install/launch has not been rechecked while worn. |
| Movement and aim | Movement, aim-math and binding tests passed. | Synthetic input/captures exercised selected Dam movement and aim paths. | Older builds were worn for movement and gun aim; current input acceptance is open. |
| Menus, watch, HUD and pointer | System, screen, pointer and settings tests passed. | Selected menu, watch, HUD and pointer paths were captured with a synthetic head. | Later pointer correction has not been re-worn. |
| Scope and cutscenes | Scope/cinema tests passed. | Later scope placement and pointer ordering were checked unattended. | 3D cinema was worn on round three; the scope redesign after its observed drift has not been re-worn. |
| Performance and gameplay | Host checks do not establish device pacing or full gameplay. | Dam and Facility measured 120 fps with zero missed frames in a 60-second unattended run on an earlier build. | Current-source pacing and full campaign acceptance are unmeasured. |

The known intro depth complaint was not reproduced in the diagnostics; a narrow sky strip at picture edges was observed. Online exit, seated mode, spatial audio, depth submission, foveation, the full Graphics page, and current-source comfort remain open. See [docs/VR_ARCHITECTURE.md](docs/VR_ARCHITECTURE.md) and [docs/PR0_READINESS.md](docs/PR0_READINESS.md).

## Build and test

On Apple Silicon macOS, install JDK 17, Android SDK platform 34, NDK 25.1.8937393, CMake 3.22.1, and Rosetta. NDK 29 is incompatible with this decompilation source's implicit-function warnings.

~~~sh
export JAVA_HOME=/opt/homebrew/opt/openjdk@17
export ANDROID_HOME=$HOME/Library/Android/sdk
sdkmanager --sdk_root=$ANDROID_HOME "platforms;android-34" "ndk;25.1.8937393" "cmake;3.22.1"
git submodule update --init --recursive
tools/steam_frame/tests/run_tests.sh
cd android
sh ./gradlew assembleFrame
~~~

The debug APK output is android/app/build/outputs/apk/frame/app-frame.apk. Playing requires a legally obtained US NTSC-U GoldenEye 007 ROM named `ge.z64` at `~/Documents/GoldenEye/ge.z64` (also `/sdcard/Documents/GoldenEye/ge.z64` on Lepton), plus player data outside the repository. No commercial game ROM, extracted game data, or saves are included.

## Rights and distribution

The root MIT notice applies to this project's original port and VR contributions. Retained third-party components keep the license terms in their own notices, as listed in [CREDITS.md](CREDITS.md) and adjacent license files. These notices do not establish a distribution grant for all reconstructed commercial game code, GoldenEye 007, ROMs, or copyrighted media. Source and media permission remains unresolved. No release package or visibility change is included in PR0.

## PR0 changelog

- Preserved the Quest root README and replaced it with a private Frame technical preview.
- Marked the older phase 0–1 Frame snapshot as historical in docs/STEAM_FRAME.md.
- Added source IDs, exact evidence levels, open acceptance, and publication blockers in docs/PR0_READINESS.md.
