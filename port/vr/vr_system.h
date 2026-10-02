/*
 * The Steam Frame system layer (standard.md section 1), compiled only with GEVR_STEAM_FRAME and
 * active only while a Steam Frame controller is the interaction profile (vr_input.cpp):
 *
 *   L View, short press   back: the game's B in menus and the launcher's back
 *   L View, hold 1 s      recentre (yaw into the body heading, world-locked panels), buzz both hands
 *   L View, hold 3 s      recentre and re-take the standing height, buzz again
 *   R Menu                the game's START (port/src/input.c)
 *
 * The timing is sfvr's (third_party/sfvr, sfvr_view.h); this file only wires it to the controller,
 * the game and the haptics.
 */
#ifndef VR_SYSTEM_H
#define VR_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Once per XR frame after the controllers were read. `focused`: the session is FOCUSED (or force_render). */
void vrSystemFrame(int focused);
/* A recentre from outside the button (the runtime moved the reference space): no buzz. */
void vrSystemRuntimeRecentre(void);
/* 1 while a short View press is still being delivered as the game's B (about 150 ms). */
int gevrSystemBack(void);
/* The test seam (host test, tests/): the same state machine fed a button level and a clock. */
unsigned vrSystemViewStep(int pressed, double now_s);

#ifdef __cplusplus
}
#endif

#endif
