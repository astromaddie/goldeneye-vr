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
} GevrDiag;

/* The settings; the file is read on the first call. Never NULL. */
const GevrDiag *gevrDiag(void);

/* Convenience for the game files, which see only C and no struct. */
int gevrDiagBootStage(void);
int gevrDiagDifficulty(void);

#ifdef __cplusplus
}
#endif

#endif
