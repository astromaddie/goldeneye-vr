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
    int   dump_scope;          /* on the Nth game frame that draws a scope: vr-dumps/vr-scope.bmp (its target), vr-scope-lens.bmp
                                  (the lens image the compositor gets) and vr-scope.txt (GL state), 0 = off */
    int   diag_gl;             /* [vr-gl]: glGetError around every game draw and at each pass's end, the pass that raised
                                  it named (first time at once, then counts every 300 game frames), 0 = off */
    int   dump_hud;            /* on the Nth game frame that submits a HUD capture: vr-dumps/vr-hud-L/R/H/P.bmp (the
                                  captures' images as copied to their quad layers), 0 = off */
    float diag_aim;            /* force_render: the gun hand raised so the scope's eyepiece is this many cm ahead of an
                                  eye (diag_aim_eye), on its line of sight (dump_scope then dumps the eyes too), 0 = parked */
    int   diag_aim_eye;        /* diag_aim's eye: 0 left (the default), 1 right */
    float diag_aim_yaw;        /* diag_aim: the gun turned about its eyepiece, degrees: yaw (positive left), */
    float diag_aim_pitch;      /*   pitch (positive up) and roll (positive right side down), so the barrel is */
    float diag_aim_roll;       /*   not the eye's line through the eyepiece, as held in a hand */
    float diag_aim_dx;         /* diag_aim: the eyepiece this many cm right of the eye's line of sight (negative left) */
    float diag_aim_swing;      /* diag_aim: degrees, the gun keeps turning about its eyepiece (yaw over 3 s, pitch
                                  over 2.2 s), so the dumps catch it moving; 0 = still */
    float diag_roll;           /* force_render: the synthetic head's roll in degrees, positive = right ear down; the
                                  parked hands stay where they are (they hang from the unrolled head) */
    int   diag_point;          /* force_render: the right hand's aim pose points at the panel that is up (the virtual
                                  screen, or the VR settings docked by the watch terminal), 0 = parked */
    int   dump_cheat_frame;    /* the dump_frame files this many XR frames after a gevr_cheat.txt word is applied (a
                                  "boom", "vrsettings"), 0 = off */
    float render_scale;        /* this run's eye resolution scale instead of the ini's RenderScale, 0 = the ini's */
    int   msaa;                /* this run's eye MSAA samples (1, 2, 4) instead of the ini's EyeMsaa, 0 = the ini's */
    int   dump_cinema_frame;   /* the dump_frame files this many XR frames after the 3D cinema is placed (a cutscene
                                  starts), with vr-dumps/vr-cinema0.bmp and vr-cinema1.bmp (its two eye images), 0 = off */
    float cinema_sep_mm;       /* the 3D cinema's camera separation in mm, 0 = the default (64) */
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
