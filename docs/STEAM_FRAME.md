# GoldenEye VR on the Valve Steam Frame

The Frame runs Android apps in **Lepton**, its Android 11 (API 30) container. This port uses an ARM64 Android APK
with OpenXR and multiview stereo. It keeps GoldenEye VR's tracked gun aiming, wrist watch, VR settings, laser pointer,
scope and cinema views, with Frame controller bindings and persistent storage.

## Build the APK

Build on x86-64 Linux or macOS, or on Windows through WSL2 with an x86-64 Ubuntu distribution. These steps use Bash.
On Apple Silicon, install Rosetta for the pinned NDK's x86-64 host tools. Steam Frame is ARM64 and is not a host for
this Android toolchain. Android's [NDK downloads](https://developer.android.com/ndk/downloads) list the available host
packages.

### Windows with WSL2

Install [WSL 2 and Ubuntu](https://learn.microsoft.com/en-us/windows/wsl/install). From PowerShell run as
Administrator, install Ubuntu with:

```powershell
wsl --install -d Ubuntu
```

Restart Windows if prompted, open Ubuntu, and create its Linux user. Do the build in Ubuntu, with the checkout and
Android SDK under Linux `~` (for example `~/src`), not under `/mnt/c`. Install the Linux JDK and tools inside Ubuntu;
a Windows JDK or Android SDK is not used by the WSL build:

```bash
sudo apt update
sudo apt install -y openjdk-17-jdk git curl unzip zip openssh-client
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
export ANDROID_HOME="$HOME/Android/Sdk"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH"
mkdir -p "$ANDROID_HOME/cmdline-tools/latest" /tmp/android-cmdline-tools
curl -fL -o /tmp/commandlinetools-linux.zip \
  https://dl.google.com/android/repository/commandlinetools-linux-15859902_latest.zip
unzip -q /tmp/commandlinetools-linux.zip -d /tmp/android-cmdline-tools
mv /tmp/android-cmdline-tools/cmdline-tools/* "$ANDROID_HOME/cmdline-tools/latest/"
```

The Linux command-line tools archive and `sdkmanager` are documented on Google's [Android Studio downloads](https://developer.android.com/studio)
and [`sdkmanager` pages](https://developer.android.com/tools/sdkmanager). Keep this Ubuntu terminal open so the
environment variables remain set.

Clone the public Frame branch into the Linux home directory:

```bash
mkdir -p ~/src && cd ~/src
git clone --branch steam-frame https://github.com/astromaddie/goldeneye-vr.git
cd goldeneye-vr
```

Install JDK 17, Android SDK command-line tools, Git, and the Android packages pinned by this branch: SDK platform 34,
build-tools 34.0.0, NDK 25.1.8937393 and CMake 3.22.1. The Gradle wrapper is 8.4 and the Android Gradle Plugin is
8.1.2. On Linux/macOS only, set these variables to the JDK 17 and Android SDK locations for that host (skip this
block if you followed the Windows/WSL setup above):

```bash
export JAVA_HOME="/path/to/jdk-17"
export ANDROID_HOME="/path/to/android-sdk"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/cmdline-tools/latest/bin:$PATH"
```

Accept the Android SDK licenses, then install the pinned packages in the same terminal:

```bash
sdkmanager --sdk_root="$ANDROID_HOME" --licenses
sdkmanager --sdk_root="$ANDROID_HOME" \
  "platform-tools" "platforms;android-34" "build-tools;34.0.0" \
  "ndk;25.1.8937393" "cmake;3.22.1"
```

For Linux and macOS, install the command-line tools for that host from Google's [Android Studio downloads](https://developer.android.com/studio)
page and put their `cmdline-tools` contents under `<android-sdk>/cmdline-tools/latest` before running the setup above.

On Windows through WSL2, clone from Ubuntu into `~/src` and run the build there. A Windows file such as
`C:\Users\WindowsUser\Documents\ge.z64` appears to WSL as `/mnt/c/Users/WindowsUser/Documents/ge.z64`; replace
`WindowsUser` with the Windows account name. The Frame game reads its ROM from `~/Documents/GoldenEye/ge.z64` on
the headset.

From the repository root, build the Frame variant:

```bash
cd android
sh ./gradlew assembleFrame
```

Gradle downloads the Khronos OpenXR loader during configuration. The APK is
`android/app/build/outputs/apk/frame/app-frame.apk`, signed with the debug key.

## Install and run

If Steam's Devkit Client is running on Windows, copy the built APK from WSL to a Windows folder so the client can
browse it. From the repository root in Ubuntu:

```bash
windows_user="$(cmd.exe /C echo %USERNAME% | tr -d '\r')"
mkdir -p "/mnt/c/Users/$windows_user/Downloads/GoldenEyeVR"
cp android/app/build/outputs/apk/frame/app-frame.apk \
  "/mnt/c/Users/$windows_user/Downloads/GoldenEyeVR/"
```

Use Valve's [SteamOS Devkit Client](https://partner.steamgames.com/doc/steamhardware/steamframe/loadgames) from Steam
on your desktop. Pair it once with the Frame: enable Developer Mode, choose **Settings > Developer > Pair new host**,
then register the Frame in the client's **Devkits** tab and confirm pairing on the headset.

For a first install, open **Devkits > Connect to devkit by IP**, choose **Title Upload**, and set:

- **Name:** `GoldenEyeVR`
- **Local Folder:** The folder containing the APK: the Windows Downloads folder copied above on WSL, or
  `android/app/build/outputs/apk/frame` on Linux/macOS.
- **Start Command:** `app-frame.apk`
- **Runtime:** `Android`

Upload and start the title. It then appears in the Frame's Steam library under **Library > Non-Steam > Devkit Game**.
For later builds, upload again with the same title name to update that library entry. Steam launches provide the
Frame controller input.

## Game data

Supply your own NTSC-U retail GoldenEye 007 ROM. The known retail image has SHA-1
`abe01e4aeb033b6c0836819f549c791b26cfde83`. Put it at `~/Documents/GoldenEye/ge.z64` on the Frame. Settings, saves
and texture-pack data are stored in the same folder; the ROM and extracted game data are not part of this repository.

To copy the ROM from your build computer, enable Developer Mode and set the SteamOS password using Valve's
[Steam Frame setup guide](https://partner.steamgames.com/doc/steamhardware/steamframe/setup). Valve's
[debugging guide](https://partner.steamgames.com/doc/steamhardware/steamframe/debugging) documents SSH as
`steamos@frame`. Set `FRAME_HOST` to `frame` or the Frame's IP address; enter the password you set when SSH asks.
These commands use the host's OpenSSH `ssh` and `scp` clients; install `openssh-client` on Ubuntu (included in the
WSL setup above).
From WSL, a ROM saved in Windows Documents is at `/mnt/c/Users/WindowsUser/Documents/ge.z64`; replace
`WindowsUser` with your Windows account name. From the build computer, run:

```bash
export FRAME_HOST=frame  # replace with the Frame's IP if its hostname does not resolve
ROM="/mnt/c/Users/WindowsUser/Documents/ge.z64"  # use the local file path on Linux/macOS
ssh "steamos@$FRAME_HOST" 'mkdir -p /home/steamos/Documents/GoldenEye'
scp "$ROM" "steamos@$FRAME_HOST:/home/steamos/Documents/GoldenEye/ge.z64"
```

## Performance

The recorded attract-loop run measured 60 game retraces per second at a 72 Hz SteamVR rate, with about 35,000–50,000
triangles per frame.
