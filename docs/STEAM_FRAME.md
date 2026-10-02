# GoldenEye VR on the Valve Steam Frame

The Frame runs Android apps in **Lepton**, an Android 11 (API 30) container with arm64-v8a,
OpenGL ES 3.2 (Mesa Zink on Turnip, Adreno 750) and SteamVR's OpenXR runtime. This port runs
the existing Android/OpenXR build there, with the multiview stereo path, from a separate
Gradle build type so the Quest build stays as upstream has it.

Status (2026-10-02): phase 0-1. The app installs, finds the ROM in persistent storage, creates
an OpenXR session, runs its frame loop with multiview and boots the game to the title/attract
loop, all proven in logcat with the headset **not worn**. Controllers and gameplay on the Frame
are untested.

## What differs from the Quest build

| Area | Quest | Frame (`assembleFrame`) |
| --- | --- | --- |
| Application id | `com.gevr.port` | `com.gevr.port.frame` |
| OpenXR loader | Meta's, `OpenXR/Libs/Android` | Khronos 1.1.63 from Maven Central, SHA-256 pinned, fetched at configure time into `third_party/` (git-ignored); `cmake/khronos_loader.cmake`. Lepton has no loader broker; it finds SteamVR via `/vendor/etc/openxr/1/active_runtime.json` |
| Manifest | `src/main` | `src/main` + `src/frame/AndroidManifest.xml`: no Quest shell metadata, no installer permission, no `vr.highperformance`/overlay-keyboard features |
| Data folder (ROM, settings, saves) | app external files dir | `/sdcard/Documents/GoldenEye` = the headset's `~/Documents/GoldenEye`, which survives Lepton resets |
| Updater | GitHub self-update | not created (`BuildConfig.STEAM_FRAME`); the launcher sees an idle status |
| CMake | - | `-DGEVR_STEAM_FRAME=ON` (also defines `GEVR_STEAM_FRAME` for C/C++) |
| SDL | - | `SDLActivity` tolerates Lepton's missing clipboard service (null-safe for everyone) |

## Build on a Mac (Apple Silicon)

One-time SDK pieces (the repo wants NDK 25.1 and CMake 3.22.1; NDK 29 fails, its Clang makes
implicit function declarations an error, which the decomp sources rely on). NDK 25 ships x86_64
host tools, so Rosetta must be installed.

```bash
export JAVA_HOME=/opt/homebrew/opt/openjdk@17 ANDROID_HOME=$HOME/Library/Android/sdk
sdkmanager --sdk_root=$ANDROID_HOME "platforms;android-34" "ndk;25.1.8937393" "cmake;3.22.1"
cd android && sh ./gradlew assembleFrame     # first build ~2 min; downloads SDL2, zlib, opus, the loader
# -> android/app/build/outputs/apk/frame/app-frame.apk   (signed with the debug key)
```

`sh ./gradlew` because the wrapper is not executable in git. The Quest builds
(`assembleDebug` / `assembleRelease`) are unchanged.

## Deploy

```bash
tools/steam_frame/deploy.sh --rom "/path/to/GoldenEye 007 (USA).z64" --register-title   # once
tools/steam_frame/deploy.sh                                                              # install + start over adb
```

`FRAME_HOST` (default `steamos@192.168.1.31`, the Frame on Wi-Fi; `steamos@10.86.200.233` over
USB networking) and `FRAME_ADB_HOST` override the targets. adb needs **Lepton Development**
running (start it from the Frame's Steam library; its adbd is on `<frame>:5555`). It deletes its
apps when it exits, so to play use the persistent **GoldenEyeVR** title in the Steam library
(a Lepton devkit title, `~/devkit-game/GoldenEyeVR/GoldenEye-VR.apk`; every later `deploy.sh`
refreshes that copy). `deploy.sh --help` lists the rest.

The ROM must be the US retail dump (SHA-1 `abe01e4aeb033b6c0836819f549c791b26cfde83`) at
`~/Documents/GoldenEye/ge.z64`. It is never committed. Settings (`goldeneye-vr.ini`), saves
(`eeprom.bin`) and the `texture-packs` index land in the same folder.

## Unattended testing (nothing worn)

A headset on a desk is in `SYNCHRONIZED` with `shouldRender=false`, so the launcher would wait
for ever and the multiview path would never draw. Two Frame-only test switches (files in the
data folder, set by `deploy.sh`):

- `--force-render` creates `gevr_force_render`: frames are drawn and submitted although the
  runtime says not to. Remove it (`--no-force-render`) before playing.
- `--autostart` writes `gevr_input.txt` (`1000 0 0 600`): the launcher's existing Start test
  hook, read from the data folder here, so the game boots without a controller. It is consumed
  on use.

`deploy.sh --logcat 40` captures the run to `scratchpad/frame-logcat.txt`. Evidence lines:
`OpenXR Runtime:`, `Session created`, `Swapchain eye=0 ... arraySize=2 multiview=1`,
`multiview FBO COMPLETE`, `Session state changed`, `[vr-frame]`, `rom: loaded ge.z64`,
`pump: N retraces, M xr frames`.

## Verified, and at which level

| Claim | Level |
| --- | --- |
| Frame APK builds on a Mac, carries the Khronos loader (not Meta's), Quest debug build and its JVM tests still build/pass | host build |
| Installs in Lepton Development; SDL activity starts after the clipboard fix | unattended device |
| Khronos loader finds SteamVR (`SteamVR/OpenXR 2.17.10`) with the OpenGL ES binding; session, 1832x1832 sRGB swapchain, `arraySize=2`, multiview FBO `COMPLETE` (0x8cd5, no GL error) | unattended device |
| Frame loop at 72 Hz (720 `xrWaitFrame`/`xrEndFrame` per 10 s), natural standby (`shouldRender=0` after frame 1, empty frames) and forced render | unattended device |
| ROM read from `~/Documents/GoldenEye/ge.z64` (`rom: loaded ge.z64`), game boots through the title into the attract loop at 60 retraces/s, ~35-50k tris/frame | unattended device (force-render + Start hook) |
| `/sdcard/Documents` access without all-files access (it is a symlink to the host folder in Lepton 2.8.14) | unattended device |
| `GoldenEyeVR` devkit title registered (`steamos-list-games` lists it) | unattended device; **not launched from the Steam library** |
| Anything about what the eyes see, controllers, comfort, performance under gameplay, audio | **not verified (nothing worn)** |

## Open problems / next

- **Controllers.** The Frame's `/interaction_profiles/valve/frame_controller_valve`
  (`XR_VALVE_frame_controller_interaction`) is not bound; the app binds simple, Touch, Vive, WMR
  and Motion, so it may or may not get input through SteamVR's remapping. Bind the Frame
  profile first.
- Not launched from the Steam library yet: confirm the title starts, writes to
  `~/Documents/GoldenEye` and gets focus when worn.
- Only 72 Hz is offered (`display: 90 Hz not offered`); render resolution is the app's 1832x1832
  (recommended 1728).
- The "Offer test builds" launcher checkbox still shows although the updater is off. Texture
  packs from the Mods page are stored in the app's files dir (wiped by a Lepton reset); the
  index in the data folder is persistent.
- Multiplayer/lobby code is untouched and only started in the background; not exercised.
- `ramrom: demo data not bound` is printed once from the attract loop; it is upstream's gap,
  not Frame-specific.
