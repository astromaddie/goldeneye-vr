/*
 * The 3D cinema for in-engine cutscenes (Steam Frame build; standard.md section 4 "Cutscenes",
 * asked for by the user 2026-10-04: "cutscenes on a 3D cinema screen with stereo depth, like the
 * GTA ports"). The geometry and the stereo maths are in vr_cinema_math.h (host-tested).
 *
 * Who does what, per game frame:
 *  - bondview2.c gevrStereoFrame decides, through a latch, that a scripted camera is showing (the
 *    mission intro flyby, the swirl into Bond, the death replay, a mission-end camera) and whether
 *    it goes to the cinema (CutsceneMode=1) or flat to the virtual screen: gevrVrCinema().
 *  - gfx_pc.cpp renders the game's frame as usual into the virtual screen's two-layer target, but
 *    with per-eye offsets (gevrCinemaPassOffsets: two cameras 64 mm apart, windows meeting 3 m
 *    ahead), so layer 0 is the left eye's picture and layer 1 the right's. The bottom messages
 *    (VR_CINEMA_BAND_* tags around hudmsgBottomRender) go into a band target instead, flat.
 *    gevrCinemaPresent copies picture and band into the cinema's swapchain (two layers).
 *  - vr_openxr.cpp vr_submit_frame, every XR frame (60 Hz game, 72-120 Hz display): the screen,
 *    world-locked where it was placed, as a projection layer rebuilt from that frame's eye poses
 *    (gevrCinemaLayer), and the transition cover (gevrCinemaFadeLayer).
 */
#ifndef VR_CINEMA_H
#define VR_CINEMA_H

#ifdef __cplusplus
extern "C" {
#endif

/* bondview2.c tags: the draws between go to the band under the picture, not into it. */
#define VR_CINEMA_BAND_BEGIN 0x56710000
#define VR_CINEMA_BAND_END   0x56710001

/* bondview2.c, once a game frame (Frame build): 0 no cutscene, 1 a cutscene shown flat on the
 * virtual screen, 2 a cutscene on the 3D cinema. unitsPerMetre: the game's view units per metre
 * (GEVR_UNITS_PER_METRE x the level's view scale). cameraMode: g_CameraMode, for the log. */
void gevrVrCinema(int state, float unitsPerMetre, int cameraMode);
/* The cutscene is on the cinema: this game frame's screen pass is the cinema pass. */
int  gevrCinemaActive(void);

/* gfx_pc.cpp: the cinema pass's per-eye offsets (gfx_opengl_set_eye_offsets layout) for the game
 * projection's P00. Returns 0 if there is no cinema. */
int  gevrCinemaPassOffsets(float p00, float out[8]);
/* gfx_pc.cpp: the screen target (w x h) is bound for the cinema pass; the band target is made and
 * cleared. Returns 0 if the band could not be made (its text then stays in the picture). */
int  gevrCinemaPassBegin(int w, int h);
/* gfx_pc.cpp, at the VR_CINEMA_BAND tags (after a flush): draw into the band, or back. */
void gevrCinemaBand(int on);
void gevrCinemaPassEnd(void);
/* gfx_pc.cpp: the finished two-layer picture (w x h) and the band into the cinema's swapchain.
 * Returns 0 on failure (the frame then goes to the virtual screen, flat). */
int  gevrCinemaPresent(unsigned srcArrayTex, int w, int h);

/* vr_openxr.cpp vr_screen_recenter: a recentre places the screen again, ahead of the head. */
void gevrCinemaRecenter(void);
/* Session end (vr_openxr.cpp, with the screen's swapchain). */
void gevrCinemaDestroy(void);

#ifdef __cplusplus
}

#include <openxr/openxr.h>
/* vr_submit_frame: the cinema as a projection layer for this XR frame's eye views (play space).
 * False when there is none (no cutscene, no picture yet, an eye behind the screen). */
bool gevrCinemaLayer(XrSpace space, const XrView eyes[2], XrCompositionLayerProjection *layer,
                     XrCompositionLayerProjectionView views[2]);
/* vr_submit_frame: the black cover of a transition (into or out of a cutscene), head-locked, for
 * this XR frame's display time. False when no transition is fading. */
bool gevrCinemaFadeLayer(XrSpace viewSpace, long long displayTimeNs, XrCompositionLayerQuad *out);
#endif

#endif
