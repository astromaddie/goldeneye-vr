/*
 * The display rate's automatic fallback (standard.md section 9; Steam Frame build, host-tested by
 * tools/steam_frame/tests/test_vr_refresh.c). Pure C: no OpenXR, no GL.
 *
 * DisplayHz (standard key refresh_rate) is 0 = Auto, the default, or a fixed 72, 90 or 120. Auto asks
 * for 120 Hz and steps down to 90 and then 72 after two 10 s windows in a row below 90% of the rate
 * the display is running at, counted only while the headset is worn (the session focused and the
 * runtime asking for frames; never in an unattended force_render run). A window with a stall longer
 * than a quarter of a second (a level load, SteamVR's loading screen) is not counted either way. A
 * fixed rate never falls back. The fallback lasts for the session: the next start asks for 120 again.
 */
#ifndef VR_REFRESH_MATH_H
#define VR_REFRESH_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_REFRESH_AUTO 0
#define GEVR_REFRESH_AUTO_HZ 120          /* what Auto asks for first */
#define GEVR_REFRESH_WINDOW_S 10.0        /* one judgement window */
#define GEVR_REFRESH_LOW_FRACTION 0.9     /* below 90% of the display's rate is a low window */
#define GEVR_REFRESH_LOW_WINDOWS 2        /* low windows in a row before a step down */
#define GEVR_REFRESH_STALL_MS 250.0       /* a window with a longer gap is a load, not counted */

typedef struct GevrRefresh {
    int setting;      /* DisplayHz: 0 Auto, or the fixed rate */
    int ceiling;      /* the highest rate still allowed this session (Auto: 120, then 90, then 72) */
    int low;          /* low windows in a row */
    double lastFps;   /* the previous low window's fps, for the log */
} GevrRefresh;

/* A DisplayHz value as stored: 0 (Auto), 72, 90 or 120; anything else is the nearest of the three
 * fixed rates, and below 0 is Auto. */
int gevrRefreshClampSetting(int hz);

/* Start a session: the rate to ask for (Auto: 120). */
int gevrRefreshInit(GevrRefresh *r, int setting);

/* The rate one step below `hz` (120 -> 90 -> 72), 0 below 72. */
int gevrRefreshStepDown(int hz);

/*
 * One finished window: `fps` the frames the app completed per second, `displayHz` the rate the display
 * is actually running at (1 / predictedDisplayPeriod), `worn` whether every frame of the window was
 * worn, `maxGapMs` the longest gap between two frames in it. Returns the rate to ask for now (a step
 * down), or 0 for no change.
 */
int gevrRefreshWindow(GevrRefresh *r, double fps, double displayHz, int worn, double maxGapMs);

#ifdef __cplusplus
}
#endif

#endif
