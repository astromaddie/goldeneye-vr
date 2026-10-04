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
(except the null-safe SDL clipboard fix). Comfort locomotion (`vr_motion.{h,c}`,
`vr_motion_math.{h,c}`, section 4) is in both builds but behind settings whose Quest defaults are off,
so the Quest build behaves as before unless its launcher turns them on; its `[vr-move]` line is
Frame-only.

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
  pointer, except behind the paused watch, where they hold the frozen, dimmed world (section 5,
  "The paused watch"). The paused watch itself is the Q-Branch terminal: its own quad layers (the
  page panel and the docked VR settings), not the virtual screen.
- Session states are logged by name. `shouldRender=false` (headset off the head) still ticks the
  game and submits empty frames; `force_render` draws anyway.
- Quitting is SteamVR's job (standard section 1, the user 2026-10-03): no menu offers it, and the
  launcher's old Quit to Steam row is gone. The app still ends cleanly when the runtime says
  `EXITING` or `LOSS_PENDING`: `MainActivity.quitApp` finishes the task and a plain thread kills the
  process about a second later (`SDLActivity.onDestroy` joins the SDL thread without a timeout, so
  nothing may depend on the main looper). `gevrQuitToSteam` (`xrRequestExitSession`, then the same
  path on `EXITING` or after 3 s) is kept for `vr-diag.ini` `exit_after_s`.
- Nothing restarts into the launcher on the Frame: the app was started by Steam, and the restart
  only ended it (device journal 2026-10-04). See "In-game VR settings" in section 5.

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
| HUD, crosshair, messages | `VR_HUD_CAPTURE_*_H`/`_R` and `VR_WEAPON_PANEL_CAPTURE_*` tags, `gfx_opengl.cpp` `gevr_capture_target_ensure` | The crosshair stays in the eye pass. Health and armour, messages, subtitles, the countdown and prompts are captured (H) onto a head-locked quad layer, the ammo counter (R) onto a panel at the right hand, the weapon panel (P) onto its own. Each capture is a two-layer target attached as two views, the image in layer 0: a one-view target is invalid with the game's `num_views = 2` programs, and Mesa dropped every capture draw (all these quads were empty on the Frame until 2026-10-04) | verified unattended 2026-10-04 (`dump_hud`: `vr-hud-R.bmp` the PP7's counter, `vr-hud-H.bmp` the countdown; `diag_gl`: no GL error in any pass). The weapon panel's capture not dumped; not worn |
| First-person gun, arms, casings | `bondview2.c` `gevrStereoGunMatrix`, `VR_CULL_OFF_*` tags | Placed from the grip pose in view space at the game frame's camera; redraw frames carry them with the hand (`gevrVrRedrawHandDelta`) | read; parked-hand dump |
| World draws between ticks (even movement, smooth stairs) | `gfx_opengl.cpp` `gfx_vr_eye_live`, `gfx_vr_eye_replay` | The world's draws take the body's move to the frame's display time through the `uReproj` branch; hand-tagged draws keep their hand delta, screen-space HUD markers are skipped | host test of the matrix; not yet on the device |
| Menus, cutscenes, intro/death cameras | `vr_screen.cpp`, `gevrStereoFrame` `want` | Not stereo: the screen quad, world-locked at 2.5 m | dump (`vr-hud.bmp`) |
| The paused watch (Frame build) | `vr_watch_terminal.cpp`, the page pass in `gfx_pc.cpp`, `gevr_watch_backdrop` | Option C: its page alone, cropped to the watch face and rendered at panel resolution, on a world-locked quad 0.9 m ahead; behind it the last stereo frame replayed from the head's pose (`uReproj` 2: hand-held draws left out, screen-locked HUD dropped), dimmed | host tests of the placement, re-centre, dock, hit and crop; verified unattended 2026-10-04 (`dump_watch_frame` in Dam: the status page sharp on the 1024 x 648 panel, alone on black in its green frame; the frozen world dimmed in stereo behind it; the VR settings docked by the cheat hook, closed with B; START resumes). Not worn |
| Sniper scope lens (and the laser's, the KF7/AR33 magnifier) | `gfx_opengl.cpp` `gfx_vr_scope_*`, `vr_openxr.cpp` scope layer | The world's draws kept from the eye pass, drawn again through the scope's camera into a 512² two-layer target attached as two views (the image is layer 0; the shader drops view 1), copied round into a quad layer on the gun. A one-view target is invalid with the game's `num_views = 2` programs: Mesa drops every draw (the lens was black on the Frame), Quest's driver let it pass | verified unattended 2026-10-04 (`dump_scope` in Dam: the target holds the zoomed world, no GL error; the same draw into a one-view target is 0x502); not worn |
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

### Comfort locomotion (launcher: COMFORT > Movement...; `port/vr/vr_motion.{h,c}`, math in `vr_motion_math.{h,c}`)

Players reported motion sickness walking with the stick. Three switches, each in `goldeneye-vr.ini`
and the launcher, so each can be put back:

1. **Even movement** (`EvenMovement`, Frame default on, Quest off).
   - *Body between ticks.* The body moves on 60 Hz ticks; at 90 Hz walking stepped 22 ms / 11 ms
     (0, 2, 0, 2... units a frame at walking pace). With it on, the frame pump lays the ticks on the
     display's own clock (`gevrMotionTickDue`: a tick is due when `predictedDisplayTime` passes the
     next 60 Hz slot; a stall over 100 ms re-anchors, never fast-forwards), and every XR frame, the
     game's own and the redraws, shows the body at `(P - tick) / 16.67 ms` of the way from the
     previous tick to the latest: one tick (16.7 ms) of locomotion latency, none on the head. The
     head's own walking (roomscale) and the multiplayer menu's head offset are not delayed.
     Snap turns (over 10 degrees a tick), recentres, teleports (`gevrNotifyTeleport`, or over 75
     units a tick), stereo coming on (level start, respawn, after a cutscene or the screen) and
     gaps in the ticks are cuts: the latest tick as drawn.
   - *How it is drawn.* The game publishes each stereo tick's camera (`bondview2.c`
     `gevrStereoPublishMotion`: position, world-to-view rotation from `matrix_4x4_set_lookat`,
     the level's view scale, body yaw, feet and floor heights). Per XR frame `gevrMotionBodyDelta`
     gives the move from that camera to the shown one in its view space,
     `c' = A Q A^T c - s A Q dpos` (`gevrBodyDeltaMatrix`). Redraws compose it under the head's
     move (`gevrVrRedrawDelta x body`); the game frame's own eye pass draws the world through the
     same `uReproj` vertex-shader branch the redraw uses (`gfx_vr_eye_live`). View-space draws do
     not take it: what a controller holds (`VR_HAND_DRAW`) keeps its hand delta, or the head's
     alone, and the HUD, crosshair and screen-space markers are skipped by the shader as before.
     A body standing still gives the identity, and the eye pass is drawn exactly as before.
     With any of the three switches on, the CPU's trivial clip reject gets a guard band of 0.06
     (NDC x w) so triangles culled for the tick's camera do not go missing at the trailing edge of
     the shown one. Known limit: the crosshair and other screen-space markers do not take the body's
     move, so on a smooth turn or strafe they can sit up to one tick off the world (at most 2 degrees
     or one tick's walk); to look at if the wearer notices.
   - *Even speed.* Bond moves by his walk animation's root motion, which surges every step (the
     0.93 smoothing leaves about a third of it). `bondhead.c` measures the root displacement per
     animation frame over whole loops of the walk (frames 9.5-27) and sprint (7.5-17) animations
     (`gevrGaitObserve`), and in stereo each tick moves by that mean times the frames it advanced
     (`gevrGaitEven`): the same distance per loop, so the same average speed and level pacing,
     without the surge. Until a whole loop has been seen (the first half second of walking after
     boot) the animation's own value is used. The `[vr-move]` line shows the learned ratio.
   - *No run boost.* The 1.0 to 1.25 speed creep after 3 s of full stick is off in solo stereo;
     the speed comes from the stick alone. Online it stays, so a match keeps one top speed. This
     makes a long run in solo play about a fifth slower than the flat game's after 3 s.
2. **Smooth stairs** (`SmoothStairs`, Frame default on, Quest off). Camera only: the body's
   physics, collision and eye height for the game are unchanged. Steps up were an IIR (0.83 a tick)
   and steps down a ballistic micro-fall that stopped dead. Per display frame the shown floor now
   eases to the floor the feet are going to, up or down alike (critically damped, 90% in about
   110 ms, never past it), while drops of more than 40 cm, falls faster than 2.5 m/s and lifts
   track the body (any gap closing in 40 ms); the hand-over carries the speed and stops at the
   floor, so a landing has no dip. The game's hard-landing dip (`vertical_bounce_adjust`, in
   `field_88`) is taken out of the shown camera.
3. **Walking sway, experimental** (`SwayStrength` 0..1, default 0 = off). Side-to-side sway of
   1.5 cm at full strength and walking speed, one cycle per two 70 cm steps, driven by distance
   walked (standing still it settles to exactly zero), and a dip of up to 1 cm per 20 cm of height
   while easing up or down stairs. Added to the shown camera in the same per-frame move.

Host tests: `tools/steam_frame/tests/test_vr_motion.c` (run by `run_tests.sh`) drive the production
functions: a constant walk through the display-time clock at 72/90/120 Hz has even frame steps (sd 0,
against 1.1-1.5 units for the latest tick alone), the cuts, the stairs follow (step up and down 90% in
111 ms each, no overshoot, a 2 m drop followed within 2 units and landing without a dip), the sway and
bob (zero at strength 0, zero once still), the draw move against a rotated, moved camera, and the gait
learner (same distance, even speed).

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

### The gun's aim (Frame build; wear feedback 2026-10-04)

The game builds the gun from the grip pose with an axis map measured on Quest Touch (right = grip
+X, up = grip -Z, back = grip +Y, so the barrel is grip -Y). On the Frame controller that barrel
pointed 30-45 degrees below where the hand points. Now each controller's rigid grip-to-aim turn is
measured whenever both poses are tracked at the same time, and kept through tracking loss:

    off = conj(grip) * aim * Rx(+90)        gun = grip * off

`Rx(+90)` maps the aim pose's axes onto the grip map, so the gun's barrel is the aim pose's -Z (the
same ray as the menu laser), its up the aim's +Y, its right the aim's +X. With no offset (Quest, or
before the first aim pose) the gun is the grip, exactly as before. Code: `vr_aim_math.{h,c}` (pure,
host-tested), `vr_input.cpp` (`gevrVrAimOffset`, by role, so left-handed mode follows; the log says
`[vr] aim offset, right controller: grip to aim N deg` once and on any change over 2 degrees),
`bondview2.c` `gevrHandAxes`/`gevrGunAxes`.

Who uses which axes:

| Gun axes (aim-corrected) | Grip axes (unchanged) |
| --- | --- |
| gun model (`gevrStereoGunMatrix`), shots and their spread (`gevrStereoShot`), crosshair (`gevrStereoAimTarget`), scope lens and scope camera (both the game's lens state and `vr_openxr.cpp`'s fallback), two-handed hold and the off hand's place on the gun, tank turret pitch | watch arm and face, the watch laser (and shooting from the watch), melee chops, motion throwing, the watch gesture, the menu layer on the hand |

The gun turns about the controller (the hand), and its fit trim (`GunOffX/Y/Z`, fitted in grip
axes on Quest) turns with it, so the gun keeps the same place in the hand and only its angle
changes. The other choice - keep the model's origin where the grip axes put it and turn only the
basis - would swing the handle about 8 cm out of the hand at a 40 degree offset (the model origin is
12 cm behind the fist). The scope lens, placed from the same origin and axes, stays on the eyepiece.

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
| R Menu | START (pause, the watch); hold 1.5 s: VR settings (the Quest build's "back to the launcher?" prompt would end the Steam-launched app, so the Frame has none) |
| R Menu + L View, together 0.5 s | VR settings, with a `ui_click` buzz on both hands. That View press is no Back and no recentre, and that Menu press no START and no Menu hold, even when let go early |
| L View | system layer: short press Back in menus; hold 1 s recentre; hold 3 s recentre + standing height |
| L D-pad | menus and the launcher: navigate; stereo play: up/down step the gun hand's weapon, left/right the off hand's |
| Bumpers | stereo play: left bumper previous, right bumper next weapon of the gun hand (as D-pad up/down); the tertiary role |

X/Y keep what the Touch left X/Y did, so a mission plays the same on both; the choice is open for
the wearer to change (the questions in section 9).

### The paused watch (Frame build; watch option C "Q-Branch Terminal", chosen 2026-10-04)

The user picked option C on 2026-10-04 in place of option D ("just another flat video of the pause
screen"). The mockup is `scratchpad/mockups/watch-options.html`, section C. D stays as a fallback:
`WatchStyle=0` in `goldeneye-vr.ini` (or **Paused watch: Big screen** in the VR settings' SCREEN
column), read as the watch comes up. The code is `port/vr/vr_watch_terminal.{h,cpp}`, the
placement math in `vr_screen_math.c` (host-tested in `test_vr_screen.c`), the page pass in
`gfx_pc.cpp`.

- **Opening.** The wrist-raise gesture or Menu, as before. With the terminal both open in stereo,
  fast and unseen (`gevrWatchOpeningByGesture`: Menu-opened watches take the gesture's path, states
  1-4 at `GEVR_WATCH_OPEN_SPEED`), so nothing shows the N64 arm-and-zoom; when the pages come up
  (state 5) the game goes to the screen pass, the world freezes behind (as in D) and the panel
  beams out of the watch hand: 0.22 s from the wrist (the off hand's grip pose; the right hand in
  left-handed mode), growing from 6% of its size, ease-out.
- **Where it hangs.** World-locked, 0.9 m ahead of the head along its yaw, its centre 15 cm below
  eye height, pitched (about 9.5 degrees) so its face looks straight back at the eyes, 60 x 38 cm, a
  flat quad layer (SteamVR on the Frame offers no cylinder layer). Placed on each open and on any
  recentre (View held, a system recentre); its height comes from the head at that moment, so it
  works seated. Lazy re-centre: once the head's yaw is more than 50 degrees from it, it glides back
  in front (exponential, 0.2 s time constant, display-time steps), its pivot following the head's
  position too, and stops within 2 degrees. Log: `[vr] watch terminal placed yaw= dist=0.90
  size=0.60x0.38 tex=1024x648 page=N (... native crop x,y wxh, the watch face; #n)`, then `[vr]
  watch terminal page=N` on page turns, `re-centring` / `settled`, `re-placed` on a recentre.
- **What it shows: the game's own page, sharp.** While the terminal holds the watch the screen pass
  is a page pass (`gfx_pc.cpp`): the display list runs into a 2048 x 1296 target, every draw outside
  `draw_watch_current_page` (tagged `VR_WATCH_PAGE_*` in `bondviewRenderWatch`) is skipped - world,
  arm, bezel, HUD - and every viewport, scissor and rectangle is mapped through a crop of the native
  frame onto the target. The crop is the rectangle the game's own LCD draw covered
  (`draw_background_health_and_armor`, tagged `VR_WATCH_LCD_*`, measured from its triangles in
  native pixels on the first page frame and kept for the open, so a page turn's zoom plays inside
  the panel as in the game), plus 3%, widened to the panel's aspect (`gevrTermCrop`). Text,
  the green backdrop, the health and armour bars, the page tabs, the 3D item (inventory) or
  controller (control options) all go through the same crop, so every page is the game's, with
  about 7x the pixels the N64 gave it and nothing around it. It is filtered down (linear blit) into
  the panel's 1024 x 648 swapchain with a 3 px green frame. Pages: Mission status/objectives, the
  briefing (M, Q, Moneypenny), Inventory, Control and Game Options (with **VR Settings...**), Abort
  mission. The first page frame only measures; if no face is ever measured the whole frame is shown
  after 10 frames (the log says so). The N64 font glyphs are still the game's bitmaps, magnified.
- **The world behind**: unchanged from option D (below): the last stereo frame frozen as the pages
  came up, drawn every XR frame from the head's pose, dimmed to 40% with the vignette, hands and HUD
  left out. Layer order: the eye layer (opaque, the paused world) then the panels; with the laser
  wanted (the docked VR settings) the panels' places are cut out of the eye image and the panels go
  under it, so the beam draws in front. A projection layer is always submitted; the virtual screen
  is not submitted while the terminal holds the watch.
- **Navigation**: the game's own, unchanged: D-pad or left stick plus A/B, grips turn pages (input.c).
  No laser on the watch's pages: rows are drawn at page-specific places by the game and selected
  through its own cursor state, and mapping a hit to them reliably for every page was not attempted.
- **Closing**: Menu, or B on the top page. The panel shrinks back into the watch hand over 0.18 s as
  the watch leaves its pages; the game's closing runs at the opening's speed over the paused world.
- **VR settings from the watch** (Game Options > VR Settings...): a second panel docked to the
  right, hinged 3 cm off the terminal's edge and turned 20 degrees in toward the player, 64 x 48 cm
  (ImGui's text about 0.85 degrees; at the terminal's 38 cm height it would have been 0.68), swinging
  out over 0.15 s. The laser is on it (`vr_screen_hit` and `vr_screen_uv_to_world` hand over to
  `gevrTermHit` / `gevrTermUvToWorld`), B closes it back to the watch's Options page. Opening them
  there does not re-place anything (`gevrVrSettingsOpen` skips its recentre over the terminal).
- **Diagnostics.** `dump_watch_frame=N` now arms with the terminal's first page and writes, N XR
  frames later, `vr-eye0.bmp`, `vr-eye1.bmp` (the paused world, dimmed) and `vr-watch.bmp` (the
  panel's 1024 x 648 image as submitted) from one game frame (`vr-dock.bmp` too if the VR settings are
  docked); `vr-hud.bmp` is not written then.

Unattended check (coordinator, for the terminal): `vr-diag.ini` with `boot_stage=33`,
`dump_watch_frame=180`, `timing=1` and `force_render=1`; once Dam is running in stereo, write `1000
0 0 6` to `~/Documents/GoldenEye/gevr_input.txt` (START for 6 frames). Expect in logcat `[vr] watch
terminal: up (WatchStyle=1)`, `[vr] paused world: frozen replay ...`, `[vr] watch terminal: panel
swapchain 1024x648`, `[vr] watch terminal placed yaw=... dist=0.90 size=0.60x0.38 tex=1024x648
page=0 (... the watch face ...)` and `[vr-dump] wrote vr-dumps/vr-eye0.bmp, vr-eye1.bmp (...
opaque_pct=100/100 vr-watch.bmp=1024x648 opaque_pct=...)`. `vr-watch.bmp` should be the mission
status page alone (green LCD, text, bars, tabs) filling the image on black; the eyes Dam darkened
with no gun, hands or HUD (the panel is a compositor layer, not in the eye images).

### The big screen (option D, `WatchStyle=0`; the default until 2026-10-04)

The mockup is `scratchpad/mockups/watch-options.html`, section D. Opening (Menu, or the wrist-raise
gesture), navigating (D-pad or stick, A/B, grips turn pages), closing (Menu, or B on the top page)
and the zoomed N64 watch image are as before; only where it is shown changed. With `WatchStyle=1`
(the default) only its placement step and the paused world behind it are still used.

- **Its own screen, world-locked.** `gevrStereoFrame` calls `gevrVrWatchScreen` (the Quest build
  keeps `gevrVrScreenHeadLock`, the screen pinned 8 degrees below the view). As the watch comes up the
  screen is placed straight ahead at the head's yaw (yaw only: a head tilted down or rolled does not
  tilt it), 1.5 m away, 1.4 m wide (53.5 degrees), its centre 10 cm below the eyes, curved
  (cylinder layer) when the runtime has `XR_KHR_composition_layer_cylinder`, else the flat quad
  (`vr_screen_math.c`, host-tested in `test_vr_screen.c`). It stays put until the watch closes, and
  is placed again on the next pause and on any recentre. The player's own screen size, distance and
  shape (`ScreenDistance`, `ScreenFov`, `ScreenCurved`) stay for menus and cutscenes; both-grip moves
  and stick resizes leave the watch's screen alone. Log: `[vr] watch screen placed yaw= dist= width=
  curved=`.
- **The world stays in stereo behind it.** The last stereo frame's draws (kept for the in-between
  redraw, issue #53) are kept again on the pause's first frame (`gfx_vr_frozen_take`): the vertex
  ring segments they use are copied on the GPU into a buffer of the same layout before the ring can
  write them again (checked with per-segment fill counts), the shader programs are checked against
  a generation count (a texture filter change deletes them: the backdrop stops). Every XR frame,
  the game's and the in-between ones, draws them from the head's pose (`gevrVrFrozenDelta`, the
  redraw's matrix against the kept camera views). Left out: what the hands hold (gun, arms, the
  watch arm), which would hang frozen in mid-air, and the screen-locked HUD and crosshair (dropped
  in the vertex shader, `uReproj` 2). Then a multiply pass darkens it to 40% (display-encoded
  values, the mockup's 60% dim) and toward 20% at the edges of the view (a soft vignette from 25
  to 60 degrees off centre); alpha becomes 1. No depth writes, no `gl_FragDepth`. Log: `[vr] paused
  world: frozen replay (dim 0.40, edges 0.20; N draws (M held by the hands left out), K of 3 vertex
  segments copied)`, or `[vr] paused world: none (why)` and the old black surround.
- **Layer order.** With the paused world in the eye image (`g_eyeBackdrop` 1) the projection layer
  goes first, opaque, and the watch's screen over it. When something reads the pointer (the VR
  settings opened from the watch: 2) the screen's surface is cleared to transparent in the eye
  image (`vr_screen_cut_eyes`) and the usual order is kept (screen, then the eye layer with alpha),
  so the beam draws in front. A projection layer is always submitted.
- **Diagnostics.** `vr-diag.ini` `dump_watch_frame=N` dumps the eyes and the screen from one game
  frame N XR frames after the watch's screen is placed; `watch_backdrop=0` turns the paused world
  off (black behind the watch, as before).

Unattended check (coordinator, with `WatchStyle=0` in `goldeneye-vr.ini`): `vr-diag.ini` with
`boot_stage=33`, `dump_watch_frame=180`, `timing=1` (and `force_render=1` if the headset is not
worn); once Dam is running in stereo, write
`1000 0 0 6` to `~/Documents/GoldenEye/gevr_input.txt` (START for 6 frames). Expect in logcat
`[vr] watch screen placed ...`, `[vr] paused world: frozen replay ...` and `[vr-dump] wrote
vr-dumps/vr-eye0.bmp, vr-eye1.bmp (... opaque_pct=100/100 screen=...)`: the eye images show Dam
darkened, with no gun, hands or HUD, and slightly offset between the eyes; `vr-hud.bmp` the watch.
`[vr-perf]` `redraw` now counts the paused frames' world too.

### In-game VR settings (Frame build; `vr_launcher.cpp`, end of file)

Openers: Menu + View held 0.5 s (`vr_system.cpp`), Menu held 1.5 s (`input.c`), and **VR
Settings...** at the bottom of the watch's Game Options page (`options.c`; A on it). A test hook too:
`vrsettings` in `gevr_cheat.txt`. Not before the game starts: the launcher is these settings then.

- **Same pages as the launcher.** The launcher's page bodies are functions now (`gevrSettingsColumns`,
  `gevrMovementPage`, `gevrThrowingPage`, `gevrCheatPage`, plus the existing Haptics page) used by
  both. One ImGui context, GL backend and 1280x960 texture, created by the launcher and kept (the
  Quest build still destroys them at Start).
- **Not blocking.** `gfx_pc.cpp` calls `gevrVrSettingsOverlayDraw` once a game frame, after the screen
  pass and before the eye pass; ImGui's backend restores the GL state it touches, and the framebuffer,
  viewport, clear colour, scissor and colour mask are restored around it. The settings texture goes to
  the virtual screen instead of the game's frame. `gevrStereoFrame` forces screen mode while they are
  up; the screen is placed in front of you as they open. Opened from the watch they dock as a second
  panel right of the Q-Branch terminal, turned 20 degrees in, with the laser on it (see "The paused
  watch"); with `WatchStyle=0` they show on the watch's own big screen instead (same place and
  shape, the paused world still behind; the screen's place is cut out of the eye layer so the laser
  draws in front of it). A front layer 10 cm closer at native resolution, as the option D mockup
  drew it, was left out: one screen is simpler and already sharp (the settings texture is 1280x960).
- **Input.** The launcher's own feeders (`feedGamepad`, `feedPointer`): the laser and trigger, the
  sticks or D-pad and A, both grips to move the screen. B, Y, a short View press or Menu close a page,
  then the settings. Buttons already down when they open do not answer until let go; the game gets
  nothing while they are up and until the buttons that closed them are let go (`input.c` returns
  early, `libultra.c` hands the PC test hook's buttons to the settings).
- **The game.** Solo: the clock stops (`lv.c`, `g_ClockTimer = 0`, as the game's own pause) and the
  frame keeps drawing underneath. Online the match cannot pause: the settings are modal for this
  player only, the netcode keeps running, the header says the match goes on, and their own input
  counts as activity (no idle kick while someone is using them).
- **Applied live:** stereo or flat, screen shape, size and distance, aim steadying, display rate
  (`vr_apply_refresh_rate` at once), turning, vignette, the Movement and Motion throwing pages,
  left-handed, swap sticks, show stats, aim no-lean, haptics, the HD pack's on/off for the session,
  Gun fit (armed for when you are back in the game), cheats (solo only, "from the next mission").
  Saved with `vrSettingsSave` on close.
- **Launcher only, left out in game:** the ROM, Mods (choosing a pack), Multiplayer, the debug-log
  report. A line at the bottom says so.
- **What used to restart into the launcher** (online Exit in the multiplayer menu, the 5-minute idle
  kick, the cheat hook's `relaunch`, any other `gevrRestartToLauncher`): the online session is left as
  before, then the game goes to the title screen its own way and the VR settings open on a note
  ("You left the online game... to join another, quit from the SteamVR menu and start GoldenEye VR
  again"). OK closes it. Nothing quits by itself.

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
| `diag_walk` | stereo play holds the move stick: `1` forward (C-up), `-1` back |
| `diag_turn` | stereo play turns smoothly at this % of the stick (-100..100), whatever the snap setting |
| `dump_watch_frame` | the `dump_frame` files N XR frames after the paused watch is placed, eyes and the watch from one game frame: with the terminal `vr-watch.bmp` (its 1024 x 648 panel; `vr-dock.bmp` when the VR settings are docked), with `WatchStyle=0` `vr-hud.bmp` (the big screen) |
| `watch_backdrop` | `0`: no paused world behind the watch (black, as before); default on |
| `dump_scope` | on the Nth game frame that draws a scope: `vr-dumps/vr-scope.bmp` (the scope target's layer 0), `vr-scope-lens.bmp` (the round lens image as handed to the compositor) and `vr-scope.txt` (target and status, view count, draws kept and in view, the pass's GL error, the same draw's error into a one-view target, camera sanity, the copy and the layer as submitted). Give the rifle with `gevr_cheat.txt` `hold17`; force_render's parked gun hand is enough. In Dam the game goes back to the PP7 about 0.7 s later, so keep N under about 30 (10 works) |
| `diag_gl` | `[vr-gl]`: `glGetError` before and after every game draw and at the end of each pass (screen pass, eye pass, scope, vignette, hub, the layer copies, the in-between redraw), the target named (eye pass, screen pass, capture L/R/H/P); the first error at each place is logged at once with the bound target's view count, then counts every 300 game frames (`no GL errors` when clean). It takes the errors it reads, so `vr-scope.txt`'s `gl_error_before` reads 0 with it on |
| `dump_hud` | on the Nth XR frame that copies a HUD capture to its quad layer: `vr-dumps/vr-hud-L/R/H/P.bmp`, each capture copied that frame (layer 0 of its target, the swapchain's size) |

`[vr-move]` every 2 s while the stereo camera moves: `step_cm mean/max/sd` (the shown camera's
step per display frame, the head's own motion not included; with even movement the sd should be near
0, without it about half the mean), `speed_cms` (the stick's speed from the last tick), `f=` (the
range of the between-ticks fraction; `1.00..1.00` when even movement is off), `clock=display|wall`,
`stairs=` mode with the frames in each, `h_minus_floor`, `sway`, and `gait=anim:ratio/loops`.

`[vr-perf]` every 10 s (standard line, sfvr formatter) with `sim`, `render`, `redraw`, `end` CPU ms,
the multiview pass as both `eye=` values, `gpu=` ms when `timing` is on, `missed` (a frame over 1.5 display
periods), then `wait=` and `period=`. Other markers: `[vr-frame]`, `[vr-sys]`, `[vr-dump]`, `[vr-diag]`,
`[VR_CTRL]`, `stereo:`, `[vr] watch terminal ...` (`up`, `placed`, `page=`, `re-centring`, `settled`,
`re-placed`, `VR settings docked`, `down`), `[vr] watch screen placed` (option D), `[vr] paused world:`.

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
| `even_movement` | `EvenMovement` | 0/1; Frame default 1, Quest 0 (port-specific, section 4) |
| `smooth_stairs` | `SmoothStairs` | 0/1; Frame default 1, Quest 0 (port-specific) |
| `sway_strength` | `SwayStrength` | 0..1, 0 = off (default); experimental, port-specific |
| `watch_style` | `WatchStyle` | `terminal` = 1 (Frame default: option C, the Q-Branch terminal), `screen` = 0 (option D, the big screen); read as the watch opens; the Quest build ignores it (port-specific) |
| `camera_shake` | none | the game's head bob and sway are off in stereo; `SwayStrength` is an opt-in experiment |
| `force_render`, `diag_yaw`, `dump_frame`, `timing`, `diag_walk`, `diag_turn`, `dump_watch_frame`, `watch_backdrop`, `dump_scope`, `diag_gl`, `dump_hud` | `vr-diag.ini` | not in the game's ini |

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
| Quit to Steam | removed from the launcher (standard section 1, 2026-10-03); EXITING / LOSS_PENDING handling kept |
| Gun aim from the aim pose (grip-to-aim offset) | built, not yet run on the device (host-tested math) |
| In-game VR settings (overlay, all three openers, solo freeze, online modal) | built, not yet run on the device |
| Menu + View chord (0.5 s, suppression of Back, recentre and START) | built, not yet run on the device (host-tested timing) |
| Menu hold 1.5 s opens VR settings (no restart prompt on the Frame) | built, not yet run on the device |
| Online Exit, idle kick: title screen and a note instead of a restart | built, not yet run on the device |
| Settings loader: integer and decimal spellings of every key | host-tested (round trip of every key the VR settings change) |
| Paused watch, option C (chosen 2026-10-04): Q-Branch terminal, the page rendered sharp on a world-locked panel 0.9 m ahead, beam in and out of the watch, lazy re-centre past 50 degrees | verified unattended 2026-10-04 (Dam, START: placed straight ahead 0.9 m, 0.60 x 0.38 m, 0.15 m below the eyes; `vr-watch.bmp` the status page, health and armour arcs, the PP7, sharp; no GL error; START again closes it). The beam animation, the re-centre and the page turns not checked on the device; not worn |
| Paused watch: VR settings docked right of the terminal, laser on the dock | docking verified unattended 2026-10-04 (the `vrsettings` cheat over the paused watch: dock swapchain 1280 x 960 placed right of the terminal, B closes and saves); the laser on it not checked |
| Paused watch: Menu opens it the gesture's way (stereo, fast); fast close | built, not yet run on the device |
| `WatchStyle` setting (terminal / big screen) | built, not yet run on the device (round-tripped in the settings test) |
| Paused watch, option D (now `WatchStyle=0`): world-locked screen, frozen dimmed stereo world behind it | verified unattended 2026-10-04 (flat quad, as SteamVR offered no cylinder layer); not worn |
| Menu laser pointer from the aim pose (Frame build) | built, not yet run on the device. Worn report 2026-10-03 on the first build: the pointer did not line up with the controller (it pointed along the grip pose's -Y, tuned on Touch) |
| Worn: scale, HUD distance, comfort, control labels | first wear 2026-10-03 on the phase-1 build (no Frame profile): the menu pointer was off; nothing else reported yet |
| Even movement: body drawn between ticks at display time, even walking speed, no run boost | verified unattended (2026-10-04, 90 Hz, Dam, `diag_walk`): `[vr-move]` steady-speed windows step a constant 5.67 cm a frame (sd 0.00) at 509 cm/s; 90 fps, 0 missed. Not worn |
| Smooth stairs: eased steps up and down, no landing dip | built, ran on the device without errors (Dam's start has no stairs, so the easing itself is host-tested only). Not worn |
| Walking sway and stair bob (experimental, off by default) | built, not yet run on the device (host-tested) |
| `[vr-move]`, `diag_walk`, `diag_turn` | built, not yet run on the device |
| Sniper scope lens shows the zoomed world (two-view scope target) | verified unattended 2026-10-04 (`dump_scope`, parked gun hand, `hold17`): target and lens image show the zoomed world, layer submitted; checked again after the merge with the HUD fix (`gl_error_before=0x0`, 86% of the target lit). Not worn; the lens's place on the gun and the red sight not checked |
| Some draw earlier in the frame than the scope pass left `GL_INVALID_OPERATION` pending (`vr-scope.txt` `gl_error_before=0x502`, 2026-10-04) | traced with `diag_gl` 2026-10-04: the HUD captures (one-view targets drawn with the two-view programs; the ammo counter's every frame it shows). Fixed with two-view capture targets; verified unattended: no GL error in any pass through play, the paused watch, the VR settings and resume, and `gl_error_before=0x0` in `vr-scope.txt` |
| HUD quads on the Frame: ammo counter, health and armour, messages, countdown, prompts, weapon panel | were empty until 2026-10-04 (every capture draw dropped, see above); verified unattended (`dump_hud`: the counter and the countdown drawn). Their size, distance and legibility not worn yet |
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
   the trigger; D-pad and A also work. (No Quit row any more: SteamVR's menu quits.)
2. START, **Dam** (6 min): is the world life size (a doorway looks 2 m)? If not, tell me the
   scale and your eye height (`PlayerHeight`). Walk, turn with the right stick (30 degree snaps),
   crouch (left stick click). Aim: one-handed, then two-handed on a rifle.
3. Weapons (2 min): A taps next weapon, grip + A back, hold A for the panel; X the left hand; D-pad
   up/down. Do the labels on screen match the buttons you press?
4. HUD and aim (1 min): is the ammo/health readable at its distance? Point at something with the
   controller held naturally: do the gun's barrel, the crosshair and the shots go where your hand
   points (wear feedback 2026-10-04: they were 30-45 degrees low)?
5. Cutscene (1 min): the intro and a mission-end cutscene on the virtual screen: distance, size,
   comfort.
6. Pause/watch and VR settings (2 min): R Menu (or the wrist raise) opens the watch: the Q-Branch
   terminal beams out of the watch and lands just ahead, a little low, with Dam dimmed behind it in
   stereo. Is the page sharp and readable at its size? Comfortable height and tilt seated and
   standing? Turn more than 50 degrees away: does it glide back in front without bothering you?
   Grips turn pages, B and a short View press go back, B on the top page closes it into the watch.
   Hold Menu + View half a second: VR settings over a paused game, a click in both hands. Change the
   turning, close with B. Hold Menu alone 1.5 s: the same settings. On the watch: Game Options >
   VR Settings... and A: they dock to the right of the terminal; the laser works there, and B
   returns to the watch's Options page.
7. Recentre (1 min): look 45 degrees off, hold View 1 s: do you feel the buzz, and is the view
   now straight ahead? Hold 3 s while standing straight.
8. Quit (30 s): the SteamVR menu's quit: back in the Steam library without a hang?

Questions: scale and `PlayerHeight`; HUD distance and legibility; any discomfort (what, when);
which button labels or positions felt wrong (X/Y, D-pad, bumpers); whether 72 Hz held (the
launcher's Show stats); did the controllers' tracking/hand poses look right.

## 10. Wear feedback 2026-10-04

Five findings from the user's session on the Frame, and one found while fixing them:

| Finding | Status |
| --- | --- |
| The gun, its shots and the crosshair aimed 30-45 degrees below where the hand points (the grip map was tuned on Touch) | fixed from the aim pose, see "The gun's aim" in section 5; built, not yet run on the device |
| VR settings could not be opened in game; Menu + View held did nothing | in-game VR settings with three openers (section 5); built, not yet run on the device |
| Holding Menu offered "back to the launcher", and yes ended the Steam-launched app ("Restarting into the launcher ... Exited!") | on the Frame the hold opens VR settings, and every restart path goes to the title screen with a note; built, not yet run on the device |
| The sniper rifle's scope lens was just black | the scope drew into a one-view target with the game's two-view programs, which Mesa rejects (`GL_INVALID_OPERATION`, every draw dropped); now a two-view target like the eye buffers. Verified unattended 2026-10-04 with `dump_scope`; not worn |
| The watch | option D chosen 2026-10-04, built (8c3565a); verified unattended the same day in Dam: screen placed straight ahead, 1.5 m away and 1.4 m wide; the frozen world replays dimmed with the hands and gun hidden; 90 fps while paused; resume closes it. SteamVR offered no cylinder layer, so the screen is the flat quad (`curved=0`). Not worn |
| (found while checking the scope) The HUD quads were empty on the Frame too: ammo counter, health and armour, messages, the countdown, prompts, the weapon panel | the same one-view mistake as the scope, in the HUD captures. Lesson: a `num_views = 2` program draws into a two-view target or nothing, and Quest's driver hides the mistake; every offscreen target the game's programs draw into must be attached as two views (read layer 0), and `diag_gl` names any pass that breaks it. Fixed and verified unattended 2026-10-04; not worn: check the HUD's size and distance at the next wear |
