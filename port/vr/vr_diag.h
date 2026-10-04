/*
 * Steam Frame bring-up diagnostics (standard.md section 8). Read once from
 * vr-diag.ini in the data folder (the working directory); on the headset that
 * is ~/Documents/GoldenEye/vr-diag.ini. tools/steam_frame/deploy.sh --diag key=val edits it.
 * Diagnostics live here and in the log only, never in the in-VR menus.
 *
 * Everything is compiled in only with GEVR_STEAM_FRAME; otherwise gevrDiag()
 * returns an all-zero struct and nothing here changes the Quest build.
 */
#ifndef VR_DIAG_H
#define VR_DIAG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GevrDiag {
    int   force_render;        /* draw and submit while the runtime says shouldRender=false, synthetic head at 1.6 m */
    float diag_yaw;            /* degrees, the synthetic head's yaw; positive turns left (OpenXR's +Y rotation) */
    int   dump_frame;          /* XR frame number to dump the eye (and screen) images at, 0 = off */
    int   dump_stereo_frame;   /* the same, counted in stereo gameplay frames (eyes drawn from the game camera) */
    int   boot_stage;          /* LEVELID to start straight in (solo mission, no menus); 0 = the title */
    int   difficulty;          /* 0 agent, 1 secret agent, 2 00 agent, 3 007; -1 = the game's default */
    int   timing;              /* [vr-perf] with per-stage CPU times, and the GPU timer query where the driver has one */
    int   autostart;           /* press the launcher's Start without a controller */
    int   present;             /* the file was read (any key may still be absent) */
    float exit_after_s;        /* quit to Steam this many seconds after the first XR frame (unattended runs), 0 = never */
    int   one_shot;            /* rename the file to vr-diag.ini.used once read, so the next launch runs normally */
    int   refresh_rate;        /* request this display rate instead of the ini's VrRefreshRate, 0 = the ini's */
    int   diag_walk;           /* in stereo play hold the move stick: 1 forward (C-up), -1 back (C-down), 0 = off */
    float diag_turn;           /* in stereo play a constant smooth turn, -100..100 % of the stick (snap turn off) */
    int   dump_watch_frame;    /* dump the eyes and the screen this many XR frames after the paused watch's screen is placed, 0 = off */
    int   watch_backdrop;      /* the paused world behind the watch's screen: 0 = default (on), 1 = on, -1 = off (black, as before) */
} GevrDiag;

/* The settings; the file is read on the first call. Never NULL. */
const GevrDiag *gevrDiag(void);

/* Convenience for the game files, which see only C and no struct. */
int gevrDiagBootStage(void);
int gevrDiagDifficulty(void);

/* diag_turn as the turn stick's -1..1, 0 when unset (always 0 outside the Frame build). */
float gevrDiagTurn(void);

/* Once per XR frame: the exit_after_s timer. */
void gevrDiagFrameTick(double nowMs);

#ifdef __cplusplus
}
#endif

#endif
