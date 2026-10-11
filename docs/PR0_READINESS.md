# PR0 readiness — GoldenEye VR

Visibility remains private. This document distinguishes source tests, historical device tests, and wearer acceptance.

The immutable PR0 test commit, supported targets and final host-check results are in [PR0_METADATA.json](PR0_METADATA.json). Full application builds and focused wearer acceptance remain pending.

## Source and test identity

- Current source revision: 61fb1cf916ba308e13ca4eeb4c667b31ceb6f4fd.
- Host test suite in the readiness record was run at that source revision: tools/steam_frame/tests/run_tests.sh.
- The GitHub root is private. The local baseline had untracked .DS_Store files in several directories; they were preserved byte-for-byte and are not part of the source test identity. No claim of anonymous root checkout is made.
- Historical Frame/wearer evidence is tied to older revisions in VR_ARCHITECTURE.md, including 3c7aa52 for round three. It is not current-head wearer acceptance.

## Supported target and dependencies

Target: Steam Frame Lepton, Android 11/API 30, arm64-v8a, GLES 3.2, SteamVR OpenXR, multiview. Build requirements: Apple Silicon macOS, JDK 17, Android platform 34, NDK 25.1.8937393, CMake 3.22.1, and Rosetta. NDK 29 is known to reject implicit declarations used by the decompilation source.

The root README contains copyable host test and assembleFrame commands. PR0 did not run a new full APK build or launch a candidate on the Frame.

## Verification by level

| Level | Evidence |
|---|---|
| Host | tools/steam_frame/tests/run_tests.sh passed binding profiles, system layer, movement, aim math, screen, cinema, upright presentation, scope, pointer, refresh-rate and settings checks at source revision 61fb1cf. |
| Unattended Frame | Historical builds installed and launched through Steam; selected rendering, menus, pause/watch, scope, pointer and cinema captures are recorded in VR_ARCHITECTURE.md. Earlier Dam/Facility runs held 120 fps with no missed frames in a 60-second synthetic-head run. Not current-source performance acceptance. |
| Worn | Older builds were worn through round three. The scope/pointer redesign and related later corrections were checked unattended only; those changes need a new wearer session. |

The depth complaint at the end of the intro remains unreproduced; a few-pixel strip of the 2D sky fill appeared at picture edges. Online exit, seated mode, spatial audio, depth submission, foveation and the full Graphics page are untested. No full campaign or complete-gameplay result is claimed.

## Fresh checkout, rights, and publication

The private root is not anonymously cloneable; no clean anonymous root checkout is claimed. The baseline was at the exact source revision above. Public dependencies are pinned in the repository and the focused host suite ran from the local source tree.

The root MIT notice applies to this project's original port and VR contributions. Retained third-party components keep the license terms in their own notices, as listed in CREDITS.md and adjacent license files. These notices do not establish a distribution grant for all reconstructed commercial game code, GoldenEye 007, ROMs, or copyrighted media. Source and media permission remains unresolved. The Frame build and user data stay external to Git. No release is published and repository visibility remains private.

## Pending wearer acceptance

On an exact candidate build, record:

- Install/update and launch from the Steam library.
- Real Frame controller profile, buttons, sticks, aim, trigger, and menu input.
- Head tracking, recenter, stereo alignment, HUD and both-eye presentation.
- Front-end menus, a representative gameplay scene, pause/resume, save/reload, and normal exit.
- Worn comfort and sustained frame pacing in named scenes at the claimed resolution and refresh rate.
- Recheck scope alignment and pointer ordering while worn; unattended captures are not acceptance.
