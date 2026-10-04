/*
 * Steam Frame diagnostics and telemetry (compiled only with GEVR_STEAM_FRAME):
 * the synthetic head for force_render, eye/HUD dumps, and the [vr-perf] line.
 * See vr_diag.h for the config and docs/STEAM_FRAME.md for how they are used.
 * vr_openxr.cpp calls these from a handful of hooks, so the Quest build is
 * untouched and the code stays out of the way of upstream merges.
 */
#ifndef VR_FRAME_H
#define VR_FRAME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- force_render: a synthetic head --------------------------------------- */
int  vrFrameForceRender(void);
/* The synthetic head in the play space (OpenXR axes, metres): 1.6 m up, yaw = diag_yaw. */
void vrFrameSynthHead(float pos[3], float quat[4]);

/* ---- dumps ----------------------------------------------------------------- */
/* Called once per XR frame that begins rendering. */
void vrFrameDumpFrameBegin(uint64_t xrFrame);
/* The stereo game camera drew this XR frame (gevrVrMarkEyesRendered(1)). */
void vrFrameDumpStereoFrame(void);
/* True while this XR frame's eye image is to be dumped (before vr_end_eye_render releases it). */
int  vrFrameDumpDue(void);
/* Write vr-eye0.bmp / vr-eye1.bmp from the two layers of the multiview swapchain image. */
void vrFrameDumpEyes(unsigned tex, int w, int h);
/* Write <name> from the framebuffer bound to GL_READ_FRAMEBUFFER (the virtual screen). */
void vrFrameDumpScreen(int w, int h);
/* Write <name> (vr-watch.bmp: the watch terminal's panel) from the framebuffer bound to
 * GL_READ_FRAMEBUFFER; counts as this dump's screen image. */
void vrFrameDumpPanel(const char *name, int w, int h);
/* The paused watch's screen (or terminal) was placed (vr_openxr.cpp): arms vr-diag.ini dump_watch_frame. */
void vrFrameDumpWatchPlaced(void);
/* State for the dump's log: called with the eye poses so the log shows what was drawn. */
void vrFrameDumpState(const float headPos[3], const float headQuat[4], const float eyePos[2][3],
                      const float eyeFov[2][4], uint64_t xrFrame, long long swapchainFormat);
/* Write the state log and report; called after the images. */
void vrFrameDumpFinish(void);

/* ---- [vr-perf] -------------------------------------------------------------- */
enum { VRPERF_RENDER = 0, VRPERF_REDRAW = 1, VRPERF_END = 2 };
/* xrWaitFrame returned: `waitMs` is how long it blocked, `periodMs` the runtime's display period. */
void vrFramePerfFrame(double waitMs, float periodMs, int shouldRender);
/* CPU time spent in a stage during the current XR frame. */
void vrFramePerfAdd(int stage, double ms);
double vrFrameNowMs(void);
/* The next eye pass is an in-between redraw (issue #53), not the game's own frame. */
void vrFramePerfSetRedraw(int redraw);
int  vrFramePerfIsRedraw(void);
/* GPU timer query around the eye pass (only with `timing` and a driver that has the extension). */
void vrFrameGpuBegin(void);
void vrFrameGpuEnd(void);

#ifdef __cplusplus
}
#endif

#endif
