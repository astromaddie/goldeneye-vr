/* See vr_motion.h. */
#include "vr_motion.h"
#include "vr_motion_math.h"

#include <math.h>
#include <string.h>

#include "system.h"   /* sysLogPrintf */

extern long long gevrVrDisplayTimeNs(void);   /* vr_openxr.cpp: this XR frame's predictedDisplayTime */

static GevrTickClock s_clock;
static int s_clockInit;
static int s_clockOn;             /* the latest decision came from the display-time clock */
static unsigned s_tickSerial;     /* game ticks run */
static unsigned s_pubSerial;      /* the tick whose camera was published last */
static long long s_tickNs;        /* the running tick's display time */
static int s_tickCut;

static GevrBodyHistory s_hist;
static float s_A[9];
static float s_scale;

static GevrStairFollow s_stairs;
static GevrSway s_sway;

static long long s_frameNs;       /* the XR frame the cached delta is for */
static int s_frameValid;
static float s_frameDelta[16];

/* [vr-move] */
static GevrStepStats s_stats;
static long long s_statsStartNs;
static float s_lastCam[3];
static int s_haveLastCam;
static unsigned s_statTicks, s_statSmooth, s_statPrompt;
static float s_statSpeed, s_gaitRatio;
static int s_gaitAnim = -1, s_gaitLoops;

int gevrMotionTickDue(int frameBegun)
{
    long long p;

    if (!s_clockInit) {
        gevrTickClockInit(&s_clock, GEVR_TICK_NS);
        s_clockInit = 1;
    }
    p = frameBegun ? gevrVrDisplayTimeNs() : 0;
    if (!VrEvenMovement || p <= 0) {
        s_clockOn = 0;
        s_clock.next_ns = 0;   /* re-anchored (a cut) when it is next used */
        return -1;
    }
    s_clockOn = 1;
    return gevrTickClockDue(&s_clock, p);
}

void gevrMotionTicked(void)
{
    s_tickSerial++;
    if (s_clockOn && s_clock.next_ns != 0) {
        gevrTickClockTicked(&s_clock);
        s_tickNs = s_clock.cur_ns;
        s_tickCut = s_clock.cut;
        s_clock.cut = 0;
    } else {
        s_tickNs = gevrVrDisplayTimeNs();
        s_tickCut = 0;
    }
}

void gevrMotionPublish(const GevrMotionTick *t)
{
    GevrBodyPose p;
    const int cut = t->cut || s_tickCut || s_pubSerial + 1 != s_tickSerial;

    memcpy(p.pos, t->pos, sizeof(p.pos));
    p.yaw_deg = t->yaw_deg;
    p.floor_y = t->floor_y;
    p.stan_y = t->stan_y;
    p.vel_y = t->vel_y;
    p.bounce_y = t->bounce_y;
    p.head_walk[0] = t->head_walk[0];
    p.head_walk[1] = t->head_walk[1];
    p.t_ns = s_tickNs;
    gevrBodyPush(&s_hist, &p, cut);
    memcpy(s_A, t->A, sizeof(s_A));
    s_scale = t->scale;
    s_pubSerial = s_tickSerial;
    s_tickCut = 0;

    if (!s_hist.cut) {
        const float dx = s_hist.cur.pos[0] - s_hist.prev.pos[0] - s_hist.cur.head_walk[0];
        const float dz = s_hist.cur.pos[2] - s_hist.prev.pos[2] - s_hist.cur.head_walk[1];
        s_statSpeed = sqrtf(dx * dx + dz * dz) * 60.0f;
    } else {
        s_statSpeed = 0.0f;
    }
    s_statTicks++;
}

void gevrMotionNoteGait(int anim, float ratio, int loops)
{
    s_gaitAnim = anim;
    s_gaitRatio = ratio;
    s_gaitLoops = loops;
}

#ifdef GEVR_STEAM_FRAME
static void gevrMotionLog(long long now)
{
    if (s_statsStartNs == 0) {
        s_statsStartNs = now;
        gevrStepStatsReset(&s_stats);
        return;
    }
    if (now - s_statsStartNs < 2000000000LL) {
        return;
    }
    /* only while moving: more than a few centimetres in the window */
    if (s_stats.n > 0 && s_stats.sum > 5.0) {
        sysLogPrintf(LOG_NOTE,
            "[vr-move] frames=%d ticks=%u step_cm mean=%.2f max=%.2f sd=%.2f speed_cms=%.0f "
            "f=%.2f..%.2f even=%d clock=%s stairs=%s smooth=%u prompt=%u h_minus_floor=%.1f sway=%.0f%% "
            "gait=%d:%.3f/%d",
            s_stats.n, s_statTicks, gevrStepStatsMean(&s_stats), s_stats.max, gevrStepStatsStddev(&s_stats),
            s_statSpeed, s_stats.fmin, s_stats.fmax, VrEvenMovement, s_clockOn ? "display" : "wall",
            VrSmoothStairs ? (s_stairs.mode == GEVR_STAIRS_PROMPT ? "prompt" : "smooth") : "off",
            s_statSmooth, s_statPrompt, VrSmoothStairs ? s_stairs.h - s_hist.cur.floor_y : 0.0f,
            VrSwayStrength * 100.0f, s_gaitAnim, s_gaitRatio, s_gaitLoops);
    }
    gevrStepStatsReset(&s_stats);
    s_statsStartNs = now;
    s_statTicks = s_statSmooth = s_statPrompt = 0;
}
#endif

int gevrMotionBodyDelta(float out[16])
{
    const long long now = gevrVrDisplayTimeNs();
    GevrBodyShown shown;
    const GevrBodyPose *cur = &s_hist.cur;
    float dt, f, h, dy, sw[3], dpos[3], dyaw;
    int onStairs;

    /* once per XR frame: the filters advance once, whoever asks (lessons.md row 4) */
    if (now > 0 && now == s_frameNs) {
        if (s_frameValid) memcpy(out, s_frameDelta, sizeof(s_frameDelta));
        return s_frameValid;
    }
    dt = (s_frameNs > 0 && now > s_frameNs) ? (float)((now - s_frameNs) * 1e-9) : 0.0f;
    s_frameNs = now;
    s_frameValid = 0;

    if (now <= 0 || !s_hist.have || s_pubSerial != s_tickSerial) {
        /* this frame's tick drew no stereo camera: start again from the next */
        gevrStairFollowReset(&s_stairs);
        gevrSwayReset(&s_sway);
        s_haveLastCam = 0;
        return 0;
    }

    f = (VrEvenMovement && s_clockOn) ? gevrTickFraction(now, cur->t_ns, GEVR_TICK_NS) : 1.0f;
    gevrBodyInterp(&s_hist, f, &shown);

    if (VrSmoothStairs) {
        h = gevrStairFollowStep(&s_stairs, shown.floor_y, shown.stan_y, shown.vel_y * 60.0f, dt);
        dy = h - cur->floor_y - cur->bounce_y;   /* and the hard landing's dip is not shown */
        if (s_stairs.mode == GEVR_STAIRS_PROMPT) s_statPrompt++; else s_statSmooth++;
        onStairs = s_stairs.mode == GEVR_STAIRS_SMOOTH;
    } else {
        gevrStairFollowReset(&s_stairs);
        h = shown.floor_y;
        dy = h - cur->floor_y;
        onStairs = fabsf(shown.floor_y - shown.stan_y) < GEVR_STAIRS_BIG && shown.vel_y * 60.0f > -GEVR_STAIRS_FALL;
    }

    {
        const float xz[2] = { shown.pos[0], shown.pos[2] };
        gevrSwayStep(&s_sway, xz, h, onStairs, VrSwayStrength, dt, sw);
    }

    dpos[0] = shown.pos[0] - cur->pos[0] + sw[0];
    dpos[1] = dy + sw[1];
    dpos[2] = shown.pos[2] - cur->pos[2] + sw[2];
    dyaw = gevrWrapDelta180(shown.yaw_deg - cur->yaw_deg);

    {
        /* [vr-move]: the shown camera's step this frame (the head's own motion not included) */
        const float cam[3] = { cur->pos[0] + dpos[0], cur->pos[1] + dpos[1], cur->pos[2] + dpos[2] };
        if (s_haveLastCam && dt > 0.0f && dt < 0.1f) {
            const float sx = cam[0] - s_lastCam[0], sy = cam[1] - s_lastCam[1], sz = cam[2] - s_lastCam[2];
            gevrStepStatsAdd(&s_stats, sqrt((double)(sx * sx + sy * sy + sz * sz)), f);
        }
        memcpy(s_lastCam, cam, sizeof(cam));
        s_haveLastCam = 1;
    }
#ifdef GEVR_STEAM_FRAME
    gevrMotionLog(now);
#endif

    if (fabsf(dpos[0]) + fabsf(dpos[1]) + fabsf(dpos[2]) < 1e-4f && fabsf(dyaw) < 1e-4f) {
        return 0;   /* the tick's own camera: drawn as before */
    }
    gevrBodyDeltaMatrix(s_A, s_scale, dpos, dyaw, s_frameDelta);
    s_frameValid = 1;
    memcpy(out, s_frameDelta, sizeof(s_frameDelta));
    return 1;
}
