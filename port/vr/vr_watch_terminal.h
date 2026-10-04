/*
 * The paused watch as a Q-Branch terminal (Steam Frame build; watch option C, chosen 2026-10-04 in
 * place of option D's big screen; mockup scratchpad/mockups/watch-options.html, section C).
 *
 * The watch opens as before (the wrist-raise gesture or Menu). As its pages come up a flat panel
 * beams out of the watch hand (0.22 s, growing from the wrist) and lands world-locked 0.9 m ahead
 * at the head's yaw, 15 cm below the eyes, pitched to face them, 60 x 38 cm (vr_screen_math.h).
 * It shows only the watch's page - the game's own draw of it (options.c draw_watch_current_page),
 * rendered at panel resolution: gfx_pc.cpp runs the paused frame's display list into a 2048 x 1296
 * target with every draw outside the page skipped and the viewport cropped to the watch face (the
 * face's rectangle measured from the game's own LCD draw a frame before), then it is filtered down
 * into the panel's 1024 x 648 swapchain with a thin green frame. The paused world stays behind it
 * in stereo, frozen and dimmed (gfx_pc.cpp gevr_watch_backdrop, unchanged from option D).
 * Navigation is the game's own (D-pad or stick, A/B, grips turn pages); the laser does not point
 * at the pages. Closing (Menu, or B on the top page) shrinks it back into the watch.
 *
 * The VR settings opened from the watch dock as a second panel to its right, turned 20 degrees in,
 * with the laser on it (vr_screen_hit and vr_screen_uv_to_world hand over to this file while it is
 * up). WatchStyle=0 in goldeneye-vr.ini keeps option D's big screen instead. Quest: not built.
 */
#ifndef VR_WATCH_TERMINAL_H
#define VR_WATCH_TERMINAL_H

#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_TERM_TEX_W 1024          /* the panel's swapchain */
#define GEVR_TERM_TEX_H 648           /* 60 x 38 cm */
#define GEVR_TERM_PASS_SCALE 2        /* the page is rendered at twice that and filtered down */

/* The setting now (bondview2.c: a terminal opens in stereo and closes faster, see there). */
int  gevrVrWatchTerminalStyle(void);
/* bondview2.c, once a game frame: the watch's page (watch_screen_index) while its pages show, else -1. */
void gevrVrWatchPage(int page);

/* vr_openxr.cpp: the paused watch is up (gevrVrWatchScreen, once a game frame); the style is
 * latched as it comes up. And its placement, as it comes up and on a recentre. */
void gevrTermWatchUp(int up);
void gevrTermPlaceAt(const float head[3], const float headQuat[4]);

/* The terminal holds the paused watch: the virtual screen is not shown. */
int  gevrTermActive(void);
/* ... and this game frame's screen pass is the page pass (gfx_pc.cpp). */
int  gevrTermPassWanted(void);
/* ... and the page is to go to the panel (its pages show). */
int  gevrTermCapture(void);

/* vr_openxr.cpp, once an XR frame after the views are located: the beam, the dock, the lazy re-centre. */
void gevrTermFrame(const float head[3], const float headQuat[4], long long displayTimeNs);

/* gfx_pc.cpp: the page pass's target (layer 0 of a two-layer array, w x h) into the panel; `crop`
 * is the native rectangle it shows and `measured` whether that came from the watch face. */
int  gevrTermPresent(unsigned srcArrayTex, int w, int h, const float crop[4], int measured);
/* gfx_pc.cpp: the VR settings' texture into the docked panel. */
int  gevrTermDockPresent(unsigned srcTex2d, int w, int h);

/* The laser (vr_openxr.cpp vr_screen_hit / vr_screen_uv_to_world): on the dock while it is up. */
int  gevrTermPointerOwned(void);
int  gevrTermHit(const float o[3], const float d[3], float *u, float *v);
void gevrTermUvToWorld(float u, float v, float out[3]);
/* The panels shown this XR frame, as corners (play space; top-left, top-right, bottom-right,
 * bottom-left): the eye image's cut-out for the laser (vr_screen_cut_eyes). Returns how many. */
int  gevrTermCorners(float corners[2][4][3]);

/* Session end (vr_openxr.cpp, with the screen's swapchain). */
void gevrTermDestroy(void);

#ifdef __cplusplus
}

#include <openxr/openxr.h>
/* vr_submit_frame: the panels' quad layers for this XR frame (0..2), opaque. */
int gevrTermLayers(XrSpace space, XrCompositionLayerQuad out[2]);
#endif

#endif
