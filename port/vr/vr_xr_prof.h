/*
 * Steam Frame: where an XR frame's time goes on the CPU (compiled only with GEVR_STEAM_FRAME).
 *
 * The OpenXR calls that cost real time on SteamVR's GLES binding (swapchain acquire, wait and
 * release, xrEndFrame, xrSyncActions and the action and space queries; skill lesson 38 and 40) are
 * wrapped at link time (-Wl,--wrap, CMakeLists.txt), so every caller is counted without touching
 * it. With vr-diag.ini `timing` a `[vr-xr]` line follows each `[vr-perf]` line (every 10 s): per XR
 * frame the swapchain cycles and their ms, the layers submitted, xrEndFrame, the input queries,
 * the game thread's own CPU time (CLOCK_THREAD_CPUTIME_ID: the busy part of the frame, lesson 50),
 * the whole process's CPU, the HUD copies and the GPU ms of the work outside the eye pass.
 */
#ifndef VR_XR_PROF_H
#define VR_XR_PROF_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    VRPROF_HUD_COPY = 0,     /* a HUD capture copied into a quad layer's image */
    VRPROF_READBACK,         /* a GPU readback that stalls the CPU (glReadPixels without a buffer) */
    VRPROF_HUD_SKIPPED,      /* a HUD layer not cycled because its capture had not changed */
    VRPROF_INPUT_SKIPPED,    /* an XR frame that reused the last game tick's buttons */
    VRPROF_COUNT_N
};

/* Count one event of the kinds above in this window. */
void vrProfCount(int what);
/* Called once per XR frame from vrFramePerfFrame (the game thread): closes the frame. */
void vrProfFrame(void);
/* The window's line (after `[vr-perf]`), and a reset. `frames` is the window's XR frame count. */
void vrProfLine(char *out, int size);
/* GPU time of work outside the eye pass (the HUD copies), summed per XR frame. */
void vrProfGpuAuxBegin(void);
void vrProfGpuAuxEnd(void);

#ifdef __cplusplus
}
#endif

#endif
