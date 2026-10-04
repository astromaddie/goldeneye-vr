/*
 * The Steam Frame system layer (standard.md section 1), compiled only with GEVR_STEAM_FRAME and
 * active only while a Steam Frame controller is the interaction profile (vr_input.cpp):
 *
 *   L View, short press       back: the game's B in menus, the launcher's and VR settings' back
 *   L View, hold 1 s          recentre (yaw into the body heading, world-locked panels), buzz both hands
 *   L View, hold 3 s          recentre and re-take the standing height, buzz again
 *   R Menu + L View, 0.5 s    the in-game VR settings (vr_launcher.cpp gevrVrSettingsOpen), ui_click buzz
 *   R Menu                    the game's START (port/src/input.c); held 1.5 s, VR settings too
 *
 * Menu + View: from the moment both are down until both are up again, that View press is neither
 * a back nor a recentre, and the Menu press is no START and no Menu hold (gevrSystemMenuChord).
 * It opens VR settings once per press, after both have been held together for 0.5 s.
 *
 * The View timing is sfvr's (third_party/sfvr, sfvr_view.h); this file wires it and the chord to
 * the controller, the game and the haptics.
 */
#ifndef VR_SYSTEM_H
#define VR_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

#define VR_SYSTEM_CHORD_HOLD_S 0.5
/* vrSystemStep's own event bit, beside sfvr_view.h's SFVR_VIEW_* */
#define VR_SYSTEM_SETTINGS 0x100u

/* Once per XR frame after the controllers were read. `focused`: the session is FOCUSED (or force_render). */
void vrSystemFrame(int focused);
/* A recentre from outside the button (the runtime moved the reference space): no buzz. */
void vrSystemRuntimeRecentre(void);
/* 1 while a short View press is still being delivered as the game's B (about 150 ms). */
int gevrSystemBack(void);
/* 1 while Menu is part of a Menu + View chord (and a moment after): input.c sends no START and
 * starts no Menu hold for that press. */
int gevrSystemMenuChord(void);

/* The test seams (host test, tests/): the same state machine and reactions, fed the two button
 * levels and a clock, and the queries at a given time. */
unsigned vrSystemStep(int view, int menu, double now_s);
unsigned vrSystemViewStep(int pressed, double now_s);
int vrSystemBackAt(double now_s);
int vrSystemMenuChordAt(double now_s);

#ifdef __cplusplus
}
#endif

#endif
