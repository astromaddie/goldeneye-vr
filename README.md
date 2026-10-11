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

The [Steam Frame guide](docs/STEAM_FRAME.md) covers building, deploying and supplying your own game data. The [architecture notes](docs/VR_ARCHITECTURE.md) describe the Frame controls, watch, scope and rendering. The Quest README is preserved in [README_QUEST.md](README_QUEST.md).

## Build and install

Build the Frame variant on Apple Silicon macOS with JDK 17 and the pinned Android SDK/NDK tools:

```sh
git clone --recurse-submodules --branch steam-frame https://github.com/astromaddie/goldeneye-vr.git
cd goldeneye-vr
export JAVA_HOME="/path/to/jdk-17"
export ANDROID_HOME="/path/to/android-sdk"
sdkmanager --sdk_root="$ANDROID_HOME" "platforms;android-34" "ndk;25.1.8937393" "cmake;3.22.1"
git submodule update --init --recursive
cd android
sh ./gradlew assembleFrame
```

The APK is written to `android/app/build/outputs/apk/frame/app-frame.apk`. Use the [Steam Frame guide](docs/STEAM_FRAME.md) to install it and place your own ROM in persistent storage.

## Credits and licenses

This fork is based on [MrSco/goldeneye-vr](https://github.com/MrSco/goldeneye-vr). The root MIT notice applies to the project's original contributions; retained third-party components keep their own notices. Those notices do not grant rights to commercial game code, media or ROMs. Bring your own game data.
