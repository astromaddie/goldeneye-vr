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
/* The synthetic head in the play space (OpenXR axes, metres): 1.6 m up, yaw = diag_yaw, then rolled by
 * diag_roll (right ear down positive). */
void vrFrameSynthHead(float pos[3], float quat[4]);
/* The same without the roll: the parked hands hang from it, so a rolled head leaves them in place. */
void vrFrameSynthBody(float quat[4]);

/* ---- the composited view (dumps) --------------------------------------------- */
/* Every swapchain image acquired (any layer): the GL texture each swapchain shows next, so a dump can
 * read every layer as submitted. `swapchain` is the XrSwapchain handle. */
void vrFrameNoteImage(uint64_t swapchain, unsigned tex);
unsigned vrFrameImageOf(uint64_t swapchain);

/* One submitted layer, in play space (vr_openxr.cpp turns view-space ones by the head). */
typedef struct VrCompLayer {
    const char *name;
    int kind;                 /* 0 projection, 1 quad, 2 not composited (a cylinder) */
    int blend;                /* XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT: premultiplied over */
    unsigned tex[2];          /* projection: per eye; quad: [0] */
    int arrayIndex[2];
    int rect[2][4];           /* x, y, w, h (GL rows from the bottom) */
    float pos[2][3];          /* projection: each view's position; quad: [0] its centre */
    float quat[2][4];         /* x y z w */
    float fovTan[2][4];       /* projection: tan of left, right, up, down */
    float size[2];            /* quad: metres */
} VrCompLayer;
/* While a dump is due and the eyes were written this frame: the layers back to front, composited
 * as the compositor does it (rotation-only projection layers, ray-cast quads, premultiplied alpha) for
 * each eye at half the eye size: vr-dumps/vr-comp0.bmp and vr-comp1.bmp, the list in the log. */
void vrFrameCompose(const VrCompLayer *layers, int n, const float eyePos[2][3], const float eyeQuat[2][4],
                    const float eyeFovTan[2][4], int eyeW, int eyeH);
/* The eyes of this XR frame were dumped (the composite goes with them). */
int  vrFrameDumpEyesWritten(void);
/* A gevr_cheat.txt word was applied (bondview2.c): arms vr-diag.ini dump_cheat_frame. */
void vrFrameDumpCheatApplied(const char *word);

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
/* The 3D cinema's screen was placed (vr_cinema.cpp): arms vr-diag.ini dump_cinema_frame. The dump then
 * wants the cinema's images too (vr-cinema0/1.bmp, written through vrFrameDumpPanel). */
void vrFrameDumpCinemaPlaced(void);
/* State for the dump's log: called with the eye poses so the log shows what was drawn. */
void vrFrameDumpState(const float headPos[3], const float headQuat[4], const float eyePos[2][3],
                      const float eyeFov[2][4], uint64_t xrFrame, long long swapchainFormat);
/* Write the state log and report; called after the images. */
void vrFrameDumpFinish(void);

/* ---- dump_scope: the sniper scope's image (vr_diag.h) ------------------------- */
/* gfx_opengl.cpp, once per scope pass: true on the pass that is to be dumped. */
int  vrFrameScopeDumpDue(void);
/* Write vr-dumps/vr-scope.bmp from layer `layer` of array texture `tex` (-1: a plain 2D texture),
 * and vr-scope.txt starting with `state`; the lens of `hand` is dumped next (vrFrameScopeDumpLens). */
void vrFrameScopeDumpTarget(int hand, unsigned tex, int layer, int w, int h, const char *state);
/* vr_openxr.cpp: whether this hand's lens copy is to be dumped now. */
int  vrFrameScopeLensDue(int hand);
/* Write vr-dumps/vr-scope-lens0.bmp and vr-scope-lens1.bmp from layers 0 and 1 of `image` (the lens's
 * two-layer swapchain image, just written: each eye's window), and add `state` to vr-scope.txt. */
void vrFrameScopeDumpLens(int hand, unsigned image, int w, int h, const char *state);
/* A line for vr-scope.txt and the log (the lens layer as submitted), only right after the lens dump. */
void vrFrameScopeNote(int hand, const char *line);

/* ---- dump_hud: the HUD captures (vr_diag.h) ------------------------------------ */
/* vr_openxr.cpp, after a HUD capture's copy to its quad layer: on the Nth XR frame that copies one,
 * writes vr-dumps/<name> from layer `layer` of array texture `tex` (-1: a plain 2D texture). */
void vrFrameHudDump(const char *name, unsigned tex, int layer, int w, int h);

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
