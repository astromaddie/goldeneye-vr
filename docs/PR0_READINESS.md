# PR0 — GoldenEye VR

**EXPERIMENTAL.** Prepared as a personal source release. This does not change the scope of the upstream or component notices. No binary release is included.

At `2023d85d1893cab3490597ce395253b01e821103`, the production VR host suite and a fresh `assembleFrame` build passed. The build fetched public dependencies and used separate shell, Java and Gradle homes, with a new temporary debug signing key. It needed no ROM or personal Git credentials. The APK contains ARM64 libraries only and passes v2 signature checks.

The APK fails the 16 KiB ZIP alignment check: its native libraries are packaged at 4 KiB offsets. Steam Frame runs Android 11; this record makes no Android 15 compatibility claim.

The archived upstream README identifies the game code under `src/` as coming from `n64decomp/007`, where no upstream license grant is present. The root MIT notice covers its stated contributions and does not grant rights to inherited game code or Nintendo game data. OpenXR and Steam Audio retain their separate upstream notices. History scans found no personal credentials or ROMs; reviewed SDL/ImGui matches were identifiers.

This check did not change or run the installed Frame game. Current controls and features are in the [README](../README.md); build identity and APK hash are in [PR0_METADATA.json](PR0_METADATA.json).
