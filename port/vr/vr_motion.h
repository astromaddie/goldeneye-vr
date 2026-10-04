/*
 * Comfort locomotion (launcher: Movement comfort; goldeneye-vr.ini EvenMovement,
 * SmoothStairs, SwayStrength). The glue between the frame pump, the game's camera
 * and the renderer; the math is vr_motion_math.{h,c}.
 *
 * Per game tick (60 Hz) the game publishes the camera it drew from and the body
 * under it (bondview2.c). Per XR frame (72/90/120 Hz) the renderer asks for the
 * move from that camera to the one to show: the body between its last two ticks
 * at the frame's predictedDisplayTime, the eased floor height, the optional sway.
 * The world's draws take that move (gfx_opengl.cpp: the eye pass's live
 * reprojection and gfx_vr_eye_replay); what is drawn in view space (the guns and
 * hands, VR_HAND_DRAW; the HUD and crosshair, screen-space markers) does not.
 */
#ifndef VR_MOTION_H
#define VR_MOTION_H

/* Settings (vr_settings_defaults.c; the Quest defaults keep today's behaviour).
 * Outside the extern "C" block, as vr_settings.h declares them for C++. */
extern int VrEvenMovement;     /* body interpolated between ticks; even walking speed; no run boost */
extern int VrSmoothStairs;     /* the camera eases small steps up and down; no landing dip */
extern float VrSwayStrength;   /* experimental walking sway and stair bob, 0 = off .. 1 */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- gevr_engine_shim.c, the frame pump ---- */
/* Whether the XR frame just begun should run a game tick, on the display-time
 * clock; -1 when that clock is not in use (even movement off, or no frame
 * begun): the pump's wall clock decides. */
int  gevrMotionTickDue(int frameBegun);
/* A game tick is about to run (whichever clock decided). */
void gevrMotionTicked(void);

/* ---- bondview2.c, the stereo camera of each tick ---- */
typedef struct GevrMotionTick {
    float pos[3];        /* the camera's world position */
    float A[9];          /* its world-to-view rotation, row-major (rows right, up, back) */
    float scale;         /* view units per world unit (D_800364CC) */
    float yaw_deg;       /* the body's yaw */
    float floor_y;       /* the feet (field_70) */
    float stan_y;        /* the floor under them (stanHeight) */
    float vel_y;         /* units per tick (field_7C) */
    float bounce_y;      /* the landing dip in this tick's eye height */
    float head_walk[2];  /* what the physical head walked the body this tick, x and z */
    int   cut;           /* a discontinuity: stereo just on, recentre, a new level */
} GevrMotionTick;
void gevrMotionPublish(const GevrMotionTick *t);

/* bondhead.c: the even gait's measured ratio, for the [vr-move] line */
void gevrMotionNoteGait(int anim, float ratio, int loops);

/* ---- gfx_pc.cpp, once per XR frame that draws the stereo eyes ---- */
/* The world draws' move from the latest tick's camera to the one to show, a GL
 * matrix in that camera's view space. 0: none (the tick's camera as drawn). */
int  gevrMotionBodyDelta(float out[16]);

#ifdef __cplusplus
}
#endif

#endif
