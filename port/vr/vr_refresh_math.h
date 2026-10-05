/*
 * The display rate (standard.md section 9; Steam Frame build, host-tested by
 * tools/steam_frame/tests/test_vr_refresh.c). Pure C: no OpenXR, no GL.
 *
 * On the Frame the app can't change the refresh rate: SteamVR runs a Steam title at the rate set for it
 * in SteamVR's per-app settings (or the display setting without one), and offers only that rate. So
 * DisplayHz (standard key refresh_rate) is 0 = Auto, the default: ask for nothing and take that rate;
 * 72, 90 or 120 are asked for once, and only if the runtime offers them. There is no fallback ladder
 * (it could not take effect). Instead the frame rate is watched: after two 10 s windows in a row below
 * 90% of the display's rate while worn (the session focused and the runtime asking for frames, never
 * an unattended force_render run; a window with a stall over a quarter of a second, a level load, is
 * not counted either way), it recommends once the per-app rate to set: one step down (120 -> 90 -> 72).
 */
#ifndef VR_REFRESH_MATH_H
#define VR_REFRESH_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_REFRESH_AUTO 0
#define GEVR_REFRESH_WINDOW_S 10.0        /* one judgement window */
#define GEVR_REFRESH_LOW_FRACTION 0.9     /* below 90% of the display's rate is a low window */
#define GEVR_REFRESH_LOW_WINDOWS 2        /* low windows in a row before the recommendation */
#define GEVR_REFRESH_STALL_MS 250.0       /* a window with a longer gap is a load, not counted */

typedef struct GevrRefresh {
    int low;           /* low windows in a row */
    int recommended;   /* the per-app rate recommended (once), 0 = none yet */
    double fps[2];     /* the last two low windows' fps, for the log */
} GevrRefresh;

/* A DisplayHz value as stored: 0 (Auto), 72, 90 or 120; anything else is the nearest of the three
 * fixed rates, and below 0 is Auto. */
int gevrRefreshClampSetting(int hz);

/* The rate one step below `hz` (120 -> 90 -> 72), 0 below 72. */
int gevrRefreshStepDown(int hz);

/* Whether to call xrRequestDisplayRefreshRateFB for `setting` given the runtime's offered rates:
 * the rate to ask for, or 0 (Auto, or not offered). */
int gevrRefreshRequestFor(int setting, const float *offered, int nOffered);

void gevrRefreshInit(GevrRefresh *r);

/*
 * One finished window: `fps` the frames the app completed per second, `displayHz` the rate the display
 * runs at (1 / predictedDisplayPeriod), `worn` whether every frame of it was worn, `maxGapMs` the
 * longest gap between two frames. Returns the per-app rate to recommend, once a session, else 0.
 */
int gevrRefreshWindow(GevrRefresh *r, double fps, double displayHz, int worn, double maxGapMs);

#ifdef __cplusplus
}
#endif

#endif
