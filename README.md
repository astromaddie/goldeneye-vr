# GoldenEye VR for Steam Frame

This is my Steam Frame fork of [GoldenEye VR](https://github.com/MrSco/goldeneye-vr). It runs on the headset through Lepton, Android, SteamVR OpenXR and multiview rendering; no PC streaming is needed.

**EXPERIMENTAL.** This is a personal-use port and remains a work in progress. It targets the Valve Steam Frame. You need your own NTSC-U GoldenEye 007 game data; no ROM, extracted game data or saves are included.

The original GoldenEye VR port supplies the core VR game, weapons, watch and scope. This fork adds the Frame build, controller support and persistent storage, along with the Frame-specific presentation and menu changes.

## VR features and Frame changes

- Head-tracked stereo play with the gun aimed from the controller pose, two-handed weapon handling and a configurable gun fit.
- Frame controller bindings, with snap or smooth turning, a left-handed layout and a comfort vignette.
- A virtual Bond watch with mission pages and in-game VR settings, plus a laser pointer for menus.
- A 3D cinema presentation for cutscenes, a tracked scope view and VR HUD elements.
- A separate Frame Gradle build and persistent game files under `~/Documents/GoldenEye`.

Multi-device netplay hasn't been tested yet.

The [Steam Frame guide](docs/STEAM_FRAME.md) covers building, installing and supplying your own game data. The [architecture notes](docs/VR_ARCHITECTURE.md) describe the Frame controls, watch, scope and rendering. The Quest README is preserved in [README_QUEST.md](README_QUEST.md).

## Build and install

Build on x86-64 Linux or macOS, or on Windows through WSL2 with an x86-64 Ubuntu distribution. The build guide uses
Bash. On Apple Silicon, install Rosetta for the pinned NDK host tools; Steam Frame itself is ARM64 and is not a host
for this Android build.

Clone the Frame branch:

```sh
mkdir -p ~/src
cd ~/src
git clone --branch steam-frame https://github.com/astromaddie/goldeneye-vr.git
cd goldeneye-vr
```

Use JDK 17 and the Android SDK versions in the [Steam Frame build and install guide](docs/STEAM_FRAME.md), then build
the Frame variant:

```sh
cd android
sh ./gradlew assembleFrame
```

The APK is `android/app/build/outputs/apk/frame/app-frame.apk`. Building creates the APK; it does not install or
launch it. The guide covers your own ROM and uploading or updating the title with Valve's SteamOS Devkit Client.

## Credits and licenses

This fork is based on [MrSco/goldeneye-vr](https://github.com/MrSco/goldeneye-vr). The root MIT notice applies to contributions within its stated scope; retained third-party components keep their own notices. Those notices do not grant rights to commercial game code, media or ROMs. Bring your own game data.
