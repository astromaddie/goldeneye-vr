# PR0 — GoldenEye VR

**EXPERIMENTAL.** The source stays private while the upstream game-code license is unresolved. No binary release was uploaded.

At `2023d85d1893cab3490597ce395253b01e821103`, the production VR host suite and a fresh `assembleFrame` build passed. The build fetched public dependencies and used separate shell, Java and Gradle homes, with a new temporary debug signing key. It needed no ROM or personal Git credentials. The APK contains ARM64 libraries only and passes v2 signature checks.

The current APK fails the 16 KiB ZIP alignment check: its native libraries are packaged at 4 KiB offsets. That needs a packaging update before claiming support for 16 KiB Android systems.

The archived upstream README identifies the game code under `src/` as coming from `n64decomp/007`, which has no upstream license. The root MIT notice covers original port contributions; it does not resolve that missing grant or license Nintendo game data. OpenXR and Steam Audio keep their separate upstream notices. History scans found no personal credentials or ROMs; reviewed SDL/ImGui matches were identifiers.

This check did not change or run the installed Frame game. Current controls and features are in the [README](../README.md); build identity and APK hash are in [PR0_METADATA.json](PR0_METADATA.json).
