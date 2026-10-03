# GoldenEye VR on the Valve Steam Frame: architecture and status

Current state, not a log. Build and deploy steps are in [STEAM_FRAME.md](STEAM_FRAME.md); the
Quest-era history is in [../STATUS.md](../STATUS.md) and `docs/archive/`. The conventions are the
Steam Frame port standard (`standard.md`, v0.1) and the evidence ladder in `verification.md`.

Nothing here has been worn yet (2026-10-03). Every status below is "verified unattended" (a headset
on a desk, force-render, logcat and dumps) or "not started"; `played in headset` is empty.

## 1. Target

| Item | Value (probed on this Frame, SteamVR OpenXR 2.17.10, Lepton 2.8.14) |
| --- | --- |
| Container | Lepton, Android 11 (API 30), arm64-v8a, 4 KB pages, no clipboard service |
| GL | GLES 3.2 through Mesa Zink on Turnip (Adreno 750); `GL_OVR_multiview2`, `GL_EXT_sRGB_write_control` |
| Runtime | SteamVR/OpenXR 2.17.10 via `/vendor/etc/openxr/1/active_runtime.json`; the app bundles the Khronos 1.1.63 loader (no broker) |
| Eyes | recommended 1728x1728; FOV L(-1.028, 0.885, 0.864, -1.048) R(-0.877, 1.033, 0.866, -1.042) rad |
| Swapchain | sRGB (`GL_SRGB8_ALPHA8`, written raw with `GL_EXT_sRGB_write_control`), 3 images, `arraySize=2` multiview |
| Refresh | 72 Hz offered; 90 is requested and logged as "not offered" |
| Controllers | `valve/frame_controller_valve` with `XR_VALVE_frame_controller_interaction` (suggest result in the log) |
| Game | N64 decomp (GoldenEye 007 USA), Fast3D GL renderer, 60 Hz tick, Perfect Dark PC port host layer |

## 2. How it fits together

Three layers, as in the playbook:

1. **XR host layer** (port-owned): `port/vr/vr_openxr.cpp` (instance, session, swapchains, frame loop,
   composition layers, recentre, refresh rate), `vr_input.cpp` (actions, profiles, haptics),
   `vr_screen.cpp` (the virtual screen), `vr_launcher.cpp` (the in-VR launcher).
2. **Bridge**: `src/game/bondview2.c` `gevrStereoFrame` reads the head pose per game frame and
   turns it into the game camera; `port/vr/vr_system.cpp` is the pure-ish system layer on sfvr
   (`third_party/sfvr`, vendored, `VERSION` = its git SHA).
3. **Game**: the decomp, with the VR hooks behind `GEVR`; the Steam Frame additions are behind
   `GEVR_STEAM_FRAME` (CMake option, set by the Gradle `frame` build type) or the runtime profile.

Frame-only files: `vr_diag.{h,c}` (vr-diag.ini), `vr_frame.{h,cpp}` (synthetic head, dumps,
`[vr-perf]`), `vr_system.{h,cpp}` (View layer). Hooks in `vr_openxr.cpp`, `vr_input.cpp`,
`gfx_pc.cpp`, `input.c`, `bondview2.c`, `main.c`, `boss.c` are all `#ifdef GEVR_STEAM_FRAME`
(except the null-safe SDL clipboard fix).

### Frame protocol

- The launcher (`gevrLauncherRun`) and the game (`gevrSchedBlockedRecv`, `gevr_engine_shim.c`) both
  run an XR frame per loop: `vr_poll_events` -> `vr_begin_frame_and_update_poses` (`xrWaitFrame`,
  `xrBeginFrame`, `xrLocateViews`, head, controllers) -> draw -> `vr_end_frame_and_submit`
  (`xrEndFrame`). `xrWaitFrame` is the only pacing.
- The game ticks at 60 Hz against a 72 Hz display: XR frames between game frames redraw the last
  stereo frame from the newest head pose (`gfx_vr_redraw_frame`, issue #53).
- Stereo gameplay renders both eyes in one pass into the two layers of one swapchain image
  (multiview). Menus, the watch, cutscenes and flat play render into a screen target and are
  submitted as a world-locked quad layer (`vr_screen.cpp`); the eye buffers then only carry the
  pointer.
- Session states are logged by name. `shouldRender=false` (headset off the head) still ticks the
  game and submits empty frames; `force_render` draws anyway.
- Quit to Steam: `xrRequestExitSession`; on `EXITING` (or 3 s) `MainActivity.quitApp` finishes the task and a plain thread kills the process about a second later (`SDLActivity.onDestroy` joins the SDL thread without a timeout, so nothing may depend on the main looper).

## 3. Camera-relative draw inventory (this Fast3D multiview path)

What was checked in the code for things that break in stereo, with the rule in force and how it was
verified. "Dump" means the unattended eye dumps of section 8.

| Thing | Where | Rule in stereo | Verified by |
| --- | --- | --- | --- |
| Per-eye frustum shear (IPD, lens asymmetry) | `gfx_pc.cpp` `vr_set_cull_offsets`, `gfx_opengl` multiview shader | The CPU clip/backface tests use the same shear numbers the shader gets (`vr_clip_margin_*`, `vr_cull_eye_dx`) | read; dump (no edge holes) |
| Room portals and per-room scissor | `bg.c` `GEVR_STEREO_PORTAL_WIDEN`, `gfx_pc.cpp` scissor widening | Portal test widened a quarter of the width; no per-room scissor; far rooms 3.3x (`GEVR_FAR_EXTEND`) | read; dump at yaw 90/180 |
| Sky | `sky.c` -> `G_GEVR_SKY_VTX` pre-projected float vertices, `gfx_sp_sky_vertex` | Clipped by the GPU per eye (`clip_rej = 0`); the sky takes the head's view like the rooms | dump (sky at every yaw) |
| Desktop aspect hacks | `gfx_update_aspect_mode`, `gfx_adjust_viewport_or_scissor` | Identity in VR: the viewport is the whole eye target, the projection is the runtime's | read |
| Full-screen fades and damage flash | `gfx_pc.cpp` `gevrStereoViewportFill` | A fill over the letterboxed player viewport is stretched to cover the eye | read |
| HUD, crosshair, messages | `VR_HUD_CAPTURE_*_H` tags, `tx_HUD` per-eye offset | Drawn in the eye pass with the HUD's own per-eye parallax (not a quad layer yet) | dump (HUD present) |
| First-person gun, arms, casings | `bondview2.c` `gevrStereoGunMatrix`, `VR_CULL_OFF_*` tags | Placed from the grip pose in view space at the game frame's camera; redraw frames carry them with the hand (`gevrVrRedrawHandDelta`) | read; parked-hand dump |
| Menus, watch, cutscenes, intro/death cameras | `vr_screen.cpp`, `gevrStereoFrame` `want` | Not stereo: the screen quad, world-locked at 2.5 m | dump (`vr-hud.bmp`) |
| Effect billboards and hit sparks | `gunfire.c`, `explosion.c` | Hit spark 4 units off the wall, smoke 16 (the game's 26/100 floated) | read (STATUS.md) |
| Fog | `bgfog.c`, `GEVR_FAR_EXTEND` | Far edge pushed out with the rooms | read |
| Water, glass | `sky.c` `WaterConcavity` 0 in stereo | Floats through the pre-projected path; reflections in view space | read (issue #30) |

Not done on the Frame (no change from the Quest build): depth submission for reprojection
(`XR_KHR_composition_layer_depth` is advertised), foveation, the HUD as a head-following quad.

## 4. Camera, scale, recentre

- World scale: 100 game units per metre times the level's visibility scale (`D_800364CC`, 0.2 on the
  Dam and Surface): `GEVR_UNITS_PER_METRE * D_800364CC`.
- Body heading: the game's yaw (`s_gevrBaseYaw`) turns with the right stick; the head adds its own
  yaw. Snap 30 degrees is the Frame build's default for a fresh `goldeneye-vr.ini` (the Quest default
  is smooth); an existing ini keeps its value.
- Recentre (View held 1 s, or a runtime reference-space change): the head's yaw becomes the body's
  heading at the next stereo frame boundary and the world-locked panels are re-placed. The standing
  height (the reference for ducking and rising, `s_gevrHeadBaseY`) is kept; View held 3 s takes it
  again. Switching stereo on, or a new level, takes it as before.
- Roomscale: head walking moves the body through the game's collision (existing); lean limits as
  upstream (25 cm per tick jump rule). Not changed.

## 5. Input

Poses: `grip/pose` places the guns, hands and the two-handed screen grab. On the Frame build the menu laser
(launcher, front-end folders, virtual screen) is the `aim/pose` ray (-Z), the runtime's pointing ray for each
controller, bound for every profile; until an aim pose has been tracked it falls back to the grip's -Y, which is
what the Quest build uses (`gevrVrPointerRayPlay`, vr_input.cpp).

Profiles suggested (the log prints each `xrSuggestInteractionProfileBindings` result):

| Profile | Needs | Notes |
| --- | --- | --- |
| `valve/frame_controller_valve` | `XR_VALVE_frame_controller_interaction` advertised | primary |
| `valve/index_controller` | core | was refused (`trackpad/click` does not exist on Index); trackpad force is the click/Menu, left a/b are X/Y |
| `oculus/touch_controller`, `htc/vive_controller`, `microsoft/motion_controller`, `khr/simple_controller` | core | unchanged |
| `hp/mixed_reality_controller`, `meta/touch_controller_plus`, `sony/playstation_vr2_controller` | their extensions (not enabled) | refused on SteamVR; harmless |

`tools/steam_frame/tests/check_bindings.py` checks every suggested path against the profile's
component list (it fails on the old Index block).

### Steam Frame control map (the physical layout is fixed; left-handed play swaps what is bound to a hand)

| Physical | Game |
| --- | --- |
| Right trigger / left trigger | right gun fires (Z) / left gun fires or aims (R); swapped by left-handed mode |
| Right grip / left grip | aim and zoom (R) / throw hold, watch L; swapped by left-handed mode |
| Left stick / right stick | move / turn (snap or smooth); clicks: left crouch toggle, right hold 1 s play-mode toggle (swapped with Swap sticks) |
| A | the gun hand's weapon: tap next, grip+tap back, hold for the selector panel (stereo); the game's A in menus |
| B | the game's B: use/reload; menus: back |
| X | the off hand's weapon cycle / left panel (stereo); the game's A in menus |
| Y | the game's B (same as B) |
| R Menu | START (pause, the watch); hold 1.5 s: "back to the launcher? A yes, B no (or a short View press), X texture pack"; Quit to Steam is in the launcher, with a confirm |
| L View | system layer: short press Back in menus; hold 1 s recentre; hold 3 s recentre + standing height |
| L D-pad | menus and the launcher: navigate; stereo play: up/down step the gun hand's weapon, left/right the off hand's |
| Bumpers | stereo play: left bumper previous, right bumper next weapon of the gun hand (as D-pad up/down); the tertiary role |

X/Y keep what the Touch left X/Y did, so a mission plays the same on both; the choice is open for
the wearer to change (section 10).

## 6. Diagnostics (Frame build only, never in the menus)

`~/Documents/GoldenEye/vr-diag.ini` (`tools/steam_frame/deploy.sh --diag KEY=VAL`):

| Key | Effect |
| --- | --- |
| `force_render` | draw and submit while `shouldRender=false`, synthetic head at 1.6 m (64 mm IPD, the runtime's FOV), hands parked, the sim keeps running |
| `diag_yaw` | the synthetic head's yaw in degrees (positive turns left); changes where the head looks relative to the body |
| `dump_frame` | from XR frame N, on the first frame that draws the eyes (a frame that draws none keeps it armed): `vr-dumps/vr-eye0.bmp`, `vr-eye1.bmp` (both multiview layers), `vr-hud.bmp` (the virtual screen as submitted), `vr-state.txt` |
| `dump_stereo_frame` | the same on the Nth stereo gameplay frame |
| `boot_stage`, `difficulty` | start in a solo mission (LEVELID, 33 = Dam; 0 agent .. 3 007) without menus |
| `autostart` | the launcher presses Start after about 2 s |
| `timing` | `[vr-perf]` GPU ms (`GL_EXT_disjoint_timer_query`, read late, never waited on) |

`[vr-perf]` every 10 s (standard line, sfvr formatter) with `sim`, `render`, `redraw`, `end` CPU ms,
the multiview pass as both `eye=` values, `gpu=` ms when `timing` is on, `missed` (a frame over 1.5 display
periods), then `wait=` and `period=`. Other markers: `[vr-frame]`, `[vr-sys]`, `[vr-dump]`, `[vr-diag]`,
`[VR_CTRL]`, `stereo:`.

## 7. Settings key map (the port's `goldeneye-vr.ini` names to the standard's)

The port's own file stays as upstream's (`vr_settings.cpp`); the standard names are what a wearer
and the docs use. Keys not listed have no standard counterpart.

| Standard key | goldeneye-vr.ini | Notes |
| --- | --- | --- |
| `turn_mode`, `snap_angle` | `SnapTurn` | 0 = smooth, else the snap angle; Frame default 30 |
| `smooth_turn_speed` | none | fixed 120 deg/s (`GEVR_TURN_DEG_PER_TICK`) |
| `vignette` / `vignette_strength` | `ComfortVignette` | 0 = off |
| `dominant_hand` | `LeftHandedMode` | plus `SwapJoysticks` |
| `haptics` | `Vibration` | the launcher's Haptics pages for the table |
| `refresh_rate` | `DisplayHz` | default 90, asked for when offered |
| `resolution_scale` | `Video.VRRenderScale` in the game config | times the recommended 1728 |
| `hud_distance` | `HudDistance` | stereo HUD parallax |
| `height` | `PlayerHeight`, `MatchCharacterHeight`, `SeatedMode` | |
| `world_scale` | `WorldScale` | |
| `force_render`, `diag_yaw`, `dump_frame`, `timing` | `vr-diag.ini` | not in the game's ini |

## 8. Status

States: **played in headset**, **verified unattended**, **not started**.

| Area | State |
| --- | --- |
| APK build on a Mac, Khronos loader, persistent devkit title | verified unattended |
| Session, multiview swapchain, 72 Hz loop, ROM from `~/Documents/GoldenEye` | verified unattended |
| force_render synthetic head, parked hands, sim running | built, not yet run on the device (the first force_render, with the real head pose, did run: STEAM_FRAME.md) |
| Stereo eye dumps in a mission (Dam) at yaw 0/90/180 | built, not yet run on the device |
| `[vr-perf]` in a mission | built, not yet run on the device |
| Frame profile suggested and active; Index suggest fixed | built, not yet run on the device (the Frame went offline, see below); controls not started |
| Frame controls, D-pad, bumpers | not started (built, untested on device) |
| System layer: View short/hold, recentre, height | host-tested state machine; not started on device |
| Quit to Steam | built, not started on device |
| Menu laser pointer from the aim pose (Frame build) | built, not yet run on the device. Worn report 2026-10-03 on the first build: the pointer did not line up with the controller (it pointed along the grip pose's -Y, tuned on Touch) |
| Worn: scale, HUD distance, comfort, control labels | first wear 2026-10-03 on the phase-1 build (no Frame profile): the menu pointer was off; nothing else reported yet |
| Depth submission, foveation, HUD quad, roomscale acceptance, spatial audio | not started |

Why "not yet run": after the commit of the diagnostics the Frame dropped off the network at about
23:50 JST on 2026-10-02 (Wi-Fi and USB both down, most likely a flat battery with nobody to charge
it) and had not returned when this was written. The first things to run when it is back:
`tools/steam_frame/tests/run_tests.sh`, then the stereo check commands in STEAM_FRAME.md.

## 9. First wear session (15 minutes)

Build: see `[vr-diag]`/version line in the launcher header (`v0.3.9-frame`, commit in the title of
the launcher). Launch **GoldenEyeVR** from the Steam library (not adb: only a Steam launch gets the
controllers). Before: `deploy.sh --no-force-render` (no `vr-diag.ini` left over; the
file is removed if you ask for `--diag-reset`).

1. Launcher (1 min): panel in front at a readable distance? Point with either controller and pull
   the trigger; D-pad and A also work. Try **Quit to Steam** > No.
2. START, **Dam** (6 min): is the world life size (a doorway looks 2 m)? If not, tell me the
   scale and your eye height (`PlayerHeight`). Walk, turn with the right stick (30 degree snaps),
   crouch (left stick click). Aim: one-handed, then two-handed on a rifle.
3. Weapons (2 min): A taps next weapon, grip + A back, hold A for the panel; X the left hand; D-pad
   up/down. Do the labels on screen match the buttons you press?
4. HUD (1 min): is the ammo/health readable at its distance? Does the crosshair sit where the gun
   points?
5. Cutscene (1 min): the intro and a mission-end cutscene on the virtual screen: distance, size,
   comfort.
6. Pause/watch (1 min): R Menu opens the watch; B and a short View press go back; hold R Menu 1.5 s
   for the prompt (do not confirm).
7. Recentre (1 min): look 45 degrees off, hold View 1 s: do you feel the buzz, and is the view
   now straight ahead? Hold 3 s while standing straight.
8. Quit (30 s): launcher > Quit to Steam > Yes: back in the Steam library without a hang?

Questions: scale and `PlayerHeight`; HUD distance and legibility; any discomfort (what, when);
which button labels or positions felt wrong (X/Y, D-pad, bumpers); whether 72 Hz held (the
launcher's Show stats); did the controllers' tracking/hand poses look right.
