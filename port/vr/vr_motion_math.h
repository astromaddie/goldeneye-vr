/*
 * Comfort locomotion math (pure: no game, GL or OpenXR state). See vr_motion.c
 * for where it is wired in and docs/VR_ARCHITECTURE.md section 4 for the design.
 *
 *  - A tick clock in display time: the game ticks at 60 Hz, the display runs at
 *    72/90/120 Hz. Each tick is stamped with its nominal display time, so the
 *    frame shown at predictedDisplayTime P draws the body as it was at P - one
 *    tick (lessons.md row 3: time comes from predictedDisplayTime, never the
 *    wall clock).
 *  - The body's last two ticks and the pose between them, with cuts (teleport,
 *    respawn, snap turn, a new level) where nothing is interpolated.
 *  - The camera's vertical follow of the floor (smooth stairs).
 *  - The experimental walking sway and stair bob.
 *  - The world draws' move from the tick's camera to the shown one.
 *  - A gait learner that turns Bond's walk animation root motion into an even
 *    speed with the same average.
 *
 * World units are the game's (about a centimetre: GEVR_UNITS_PER_METRE 100).
 * Host tests: tools/steam_frame/tests/test_vr_motion.c.
 */
#ifndef VR_MOTION_MATH_H
#define VR_MOTION_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_TICK_NS 16666667LL          /* the game's 60 Hz retrace */

/* ---- the tick clock --------------------------------------------------------- */

typedef struct GevrTickClock {
    int64_t period_ns;
    int64_t next_ns;   /* nominal display time of the next tick; 0 = not anchored */
    int64_t cur_ns;    /* nominal display time of the latest tick */
    int     cut;       /* the latest tick re-anchored the timeline */
} GevrTickClock;

void gevrTickClockInit(GevrTickClock *c, int64_t period_ns);
/* Whether the XR frame shown at display_ns should run a game tick. Re-anchors the
 * timeline (a cut) when it is unset, more than 100 ms behind, or more than three
 * ticks ahead (a clock reset). */
int  gevrTickClockDue(GevrTickClock *c, int64_t display_ns);
/* The tick that was due ran: it is stamped with its nominal time. */
void gevrTickClockTicked(GevrTickClock *c);
/* How far the frame shown at display_ns is from the previous tick's pose (0) to
 * the latest tick's (1): (display - tick) / period, clamped to 0..1. */
float gevrTickFraction(int64_t display_ns, int64_t tick_ns, int64_t period_ns);

/* ---- the body's ticks --------------------------------------------------------- */

typedef struct GevrBodyPose {
    float pos[3];        /* the camera's world position as the tick drew it */
    float yaw_deg;       /* the body's yaw (bondview2.c s_gevrBaseYaw) */
    float floor_y;       /* the body's feet: physics (player field_70) */
    float stan_y;        /* the floor under the body (player stanHeight) */
    float vel_y;         /* the feet's vertical speed, units per tick (field_7C) */
    float bounce_y;      /* the landing dip in the eye height this tick, units */
    float head_walk[2];  /* x, z: what the physical head walked the body this tick */
    int64_t t_ns;        /* the tick's nominal display time */
} GevrBodyPose;

typedef struct GevrBodyHistory {
    GevrBodyPose prev, cur;
    int have;            /* cur is set */
    int cut;             /* the latest push cut the interpolation (prev = cur) */
} GevrBodyHistory;

#define GEVR_BODY_JUMP_CUT 75.0f       /* units in one tick: a teleport, not a walk */
#define GEVR_BODY_YAW_CUT  10.0f       /* degrees in one tick: a snap turn or recentre */

void gevrBodyReset(GevrBodyHistory *h);
/* A new tick. cut: the caller knows of a discontinuity (stereo just came on,
 * recentre, a new level). Also cut: a jump of more than GEVR_BODY_JUMP_CUT, a
 * gap of more than two ticks. A yaw step over GEVR_BODY_YAW_CUT cuts the yaw only. */
void gevrBodyPush(GevrBodyHistory *h, const GevrBodyPose *p, int cut);

typedef struct GevrBodyShown {
    float pos[3];        /* x, z interpolated; y the latest tick's */
    float yaw_deg;
    float floor_y, stan_y, vel_y;
} GevrBodyShown;

/* The body f of the way from the previous tick to the latest. What the head
 * walked in the latest tick is not interpolated: the head already showed it. */
void gevrBodyInterp(const GevrBodyHistory *h, float f, GevrBodyShown *out);

/* Wrap a degree difference into -180..180. */
float gevrWrapDelta180(float deg);

/* ---- smooth stairs: the camera's vertical follow -------------------------------- */

enum { GEVR_STAIRS_SMOOTH = 0, GEVR_STAIRS_PROMPT = 1 };

typedef struct GevrStairFollow {
    float h;             /* the floor height the camera shows */
    float v;             /* its speed, units/s (smooth mode) */
    float off;           /* prompt mode: h - floor, decaying */
    int   mode;
    int   valid;
} GevrStairFollow;

#define GEVR_STAIRS_OMEGA     32.0f   /* 1/s, critically damped: 90% of a step in about 120 ms */
#define GEVR_STAIRS_BIG       40.0f   /* units (cm): a bigger gap to the floor is a drop or a lift, tracked promptly */
#define GEVR_STAIRS_FALL      250.0f  /* units/s: falling faster than a stair's micro-fall is a real fall */
#define GEVR_STAIRS_PROMPT_TAU 0.04f  /* s: entering prompt mode, the camera's gap to the body closes this fast */

void gevrStairFollowReset(GevrStairFollow *s);
/* One display frame. floor_y: the body's feet (physics), stan_y: the floor under
 * it, vel_y_per_s: the feet's vertical speed, dt_s: time since the last frame.
 * Small steps (up or down) ease to the floor symmetrically; drops, falls and
 * lifts track the body. Returns the floor height to show. */
float gevrStairFollowStep(GevrStairFollow *s, float floor_y, float stan_y, float vel_y_per_s, float dt_s);

/* ---- experimental: walking sway and stair bob ------------------------------------ */

typedef struct GevrSway {
    float last_xz[2];
    float last_h;
    float dir[2];        /* smoothed travel direction */
    float dist;          /* distance walked (units): drives the sway's phase */
    float env;           /* 0..1: sway amplitude, from the walking speed */
    float vdist;         /* height climbed or descended on stairs: the bob's phase */
    float venv;          /* 0..1: bob amplitude */
    int   valid;
} GevrSway;

#define GEVR_SWAY_AMPL_CM   1.5f    /* side to side at full strength and full speed */
#define GEVR_SWAY_STEP_CM   70.0f   /* a step; one sway cycle is two */
#define GEVR_SWAY_FULL_CMS  150.0f  /* walking this fast gives the full sway */
#define GEVR_BOB_AMPL_CM    1.0f    /* the stair bob's depth at full strength */
#define GEVR_BOB_RISE_CM    20.0f   /* one bob per this much height */
#define GEVR_BOB_FULL_CMS   30.0f   /* climbing this fast gives the full bob */

void gevrSwayReset(GevrSway *s);
/* One display frame. xz: the shown body position, h: the shown floor height,
 * on_stairs: the vertical follow is easing small steps (not a fall or a lift),
 * strength 0..1 (0 = off: out is zero and the state resets). out: x, y, z offset
 * in world units to add to the camera. Driven by distance, not time: standing
 * still the phase stops and the amplitude decays to exactly zero. */
void gevrSwayStep(GevrSway *s, const float xz[2], float h, int on_stairs, float strength, float dt_s, float out[3]);

/* ---- the world draws' move -------------------------------------------------------- */

/* The rotation the body yaw gives the camera in the world (bondview2.c
 * gevrStereoLook: a quaternion of -yaw about +Y), row-major 3x3. */
void gevrBodyYawMatrix(float yaw_deg, float R[9]);

/* The move of a point fixed in the world from the tick camera's view space to the
 * shown camera's, a GL (column-major) 4x4. A: the tick camera's world-to-view
 * rotation, row-major (rows: right, up, back); scale: view units per world unit
 * (the level's D_800364CC); dpos: shown camera position - tick camera position
 * (world units); dyaw_deg: shown body yaw - tick body yaw.
 *   c' = A Q A^T c - scale A Q dpos,  Q = Rb(tick) Rb(shown)^T. */
void gevrBodyDeltaMatrix(const float A[9], float scale, const float dpos[3], float dyaw_deg, float out[16]);

/* out = a * b, GL column-major 4x4. */
void gevrMat4Mul(const float a[16], const float b[16], float out[16]);

/* ---- even walking speed --------------------------------------------------------- */

typedef struct GevrGaitLearner {
    float sum_disp;      /* this loop so far */
    float sum_frames;
    float ratio;         /* mean root displacement per animation frame over whole loops */
    int   loops;         /* whole loops measured (capped, a running mean) */
} GevrGaitLearner;

void gevrGaitReset(GevrGaitLearner *g);
/* One tick of the walk loop: disp, the animation's root displacement this tick;
 * frames, the animation frames it advanced; loop_len, the frames in one loop. */
void gevrGaitObserve(GevrGaitLearner *g, float disp, float frames, float loop_len);
/* The displacement for `frames` at the loop's mean rate; `disp` until a whole loop
 * has been measured. Over a loop it sums to what the animation itself moves. */
float gevrGaitEven(const GevrGaitLearner *g, float disp, float frames);

/* ---- the [vr-move] statistics ----------------------------------------------------- */

typedef struct GevrStepStats {
    int n;
    double sum, sumsq, max;
    float fmin, fmax;
} GevrStepStats;

void gevrStepStatsReset(GevrStepStats *s);
void gevrStepStatsAdd(GevrStepStats *s, double step, float f);
double gevrStepStatsMean(const GevrStepStats *s);
double gevrStepStatsStddev(const GevrStepStats *s);

#ifdef __cplusplus
}
#endif

#endif
