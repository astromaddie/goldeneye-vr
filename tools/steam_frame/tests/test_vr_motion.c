/*
 * Host tests for comfort locomotion's math (port/vr/vr_motion_math.c), through
 * the production functions: the display-time tick clock and the body between
 * ticks, the cuts, the stairs follow, the sway and bob, the world draws' move,
 * the even gait. Run by run_tests.sh.
 */
#include "vr_motion_math.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int s_fail, s_checks;

#define CHECK(cond, ...) do { s_checks++; if (!(cond)) { s_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int near(double a, double b, double eps) { return fabs(a - b) <= eps; }

/* ---- walking at a constant stick speed through the display-time clock ---------- */

typedef struct WalkResult {
    double mean, sd, max, min;
    int ticks, frames;
    float fmin, fmax;
} WalkResult;

/* hz: display rate; v: units per tick; interp: 0 shows the latest tick (today's) */
static WalkResult walk(double hz, float v, int interp, double seconds)
{
    GevrTickClock c;
    GevrBodyHistory h;
    GevrStepStats st;
    WalkResult r;
    const int64_t period = (int64_t)(1e9 / hz + 0.5);
    int64_t p = 1000000000LL;
    int n = 0, frame;
    float lastx = 0.0f;
    int have = 0;
    double minStep = 1e9;

    gevrTickClockInit(&c, GEVR_TICK_NS);
    gevrBodyReset(&h);
    gevrStepStatsReset(&st);
    memset(&r, 0, sizeof(r));
    for (frame = 0; frame < (int)(seconds * hz); frame++, p += period) {
        GevrBodyShown s;
        float f;
        if (gevrTickClockDue(&c, p)) {
            GevrBodyPose pose;
            gevrTickClockTicked(&c);
            memset(&pose, 0, sizeof(pose));
            pose.pos[0] = v * (float)n++;
            pose.t_ns = c.cur_ns;
            gevrBodyPush(&h, &pose, c.cut);
            c.cut = 0;
        }
        f = interp ? gevrTickFraction(p, h.cur.t_ns, GEVR_TICK_NS) : 1.0f;
        gevrBodyInterp(&h, f, &s);
        if (have && frame > (int)hz / 2) {   /* after the first half second */
            const double step = fabs(s.pos[0] - lastx);
            gevrStepStatsAdd(&st, step, f);
            if (step < minStep) minStep = step;
        }
        lastx = s.pos[0];
        have = 1;
    }
    r.mean = gevrStepStatsMean(&st);
    r.sd = gevrStepStatsStddev(&st);
    r.max = st.max;
    r.min = minStep;
    r.ticks = n;
    r.frames = frame;
    r.fmin = st.fmin;
    r.fmax = st.fmax;
    return r;
}

static void test_even_walk(void)
{
    const double rates[] = { 72.0, 90.0, 120.0 };
    int i;

    for (i = 0; i < 3; i++) {
        const WalkResult even = walk(rates[i], 3.0f, 1, 4.0);
        const WalkResult today = walk(rates[i], 3.0f, 0, 4.0);
        const double want = 3.0 * 60.0 / rates[i];   /* units per display frame */

        CHECK(even.ticks >= 239 && even.ticks <= 241, "%.0f Hz: %d ticks in 4 s, want 240", rates[i], even.ticks);
        CHECK(near(even.mean, want, 0.01), "%.0f Hz: mean step %.4f, want %.4f", rates[i], even.mean, want);
        CHECK(even.sd < 0.05 * want, "%.0f Hz: interpolated steps uneven, sd %.4f (mean %.4f)", rates[i], even.sd, even.mean);
        CHECK(even.max < want * 1.06 && even.min > want * 0.94, "%.0f Hz: steps %.3f..%.3f, want %.3f",
              rates[i], even.min, even.max, want);
        CHECK(even.fmin >= 0.0f && even.fmax <= 1.0f, "fraction out of range %.3f..%.3f", even.fmin, even.fmax);
        if (rates[i] != 120.0) {
            /* today's: whole ticks only - 0 and 2 steps at 90 Hz, 0 and 1 at 72 */
            CHECK(today.sd > 0.3 * want, "%.0f Hz: expected today's judder, sd %.4f", rates[i], today.sd);
        }
        printf("  walk %3.0f Hz: even step %.3f sd %.4f | latest-tick step %.3f sd %.3f max %.3f\n",
               rates[i], even.mean, even.sd, today.mean, today.sd, today.max);
    }
}

/* ---- the tick clock -------------------------------------------------------------- */

static void test_tick_clock(void)
{
    GevrTickClock c;
    int64_t p = 5000000000LL;

    gevrTickClockInit(&c, GEVR_TICK_NS);
    CHECK(gevrTickClockDue(&c, p) && c.cut, "first frame anchors (a cut) and ticks");
    gevrTickClockTicked(&c);
    c.cut = 0;
    CHECK(c.cur_ns == p && c.next_ns == p + GEVR_TICK_NS, "stamped with its nominal time");
    CHECK(!gevrTickClockDue(&c, p + 11111111) && !c.cut, "a frame before the next tick redraws");
    CHECK(gevrTickClockDue(&c, p + 22222222), "the next frame past it ticks");
    gevrTickClockTicked(&c);
    CHECK(c.cur_ns == p + GEVR_TICK_NS, "the tick keeps its nominal time, not the frame's");
    /* a stall: re-anchored, never fast-forwarded */
    CHECK(gevrTickClockDue(&c, p + 500000000LL) && c.cut, "a 0.5 s stall re-anchors");
    gevrTickClockTicked(&c);
    CHECK(c.cur_ns == p + 500000000LL, "re-anchored at the frame");
    /* fraction */
    CHECK(near(gevrTickFraction(100, 100, GEVR_TICK_NS), 0.0, 1e-6), "fraction 0 at the tick");
    CHECK(near(gevrTickFraction(100 + GEVR_TICK_NS / 2, 100, GEVR_TICK_NS), 0.5, 1e-4), "fraction 0.5 half way");
    CHECK(gevrTickFraction(100 + 3 * GEVR_TICK_NS, 100, GEVR_TICK_NS) == 1.0f, "clamped to 1, never extrapolated");
    CHECK(gevrTickFraction(50, 100, GEVR_TICK_NS) == 0.0f, "clamped to 0");
}

/* ---- cuts ---------------------------------------------------------------------- */

static void push(GevrBodyHistory *h, float x, float yaw, int64_t t, int cut, float hw)
{
    GevrBodyPose p;
    memset(&p, 0, sizeof(p));
    p.pos[0] = x;
    p.yaw_deg = yaw;
    p.head_walk[0] = hw;
    p.t_ns = t;
    gevrBodyPush(h, &p, cut);
}

static void test_cuts(void)
{
    GevrBodyHistory h;
    GevrBodyShown s;

    gevrBodyReset(&h);
    push(&h, 0.0f, 10.0f, GEVR_TICK_NS, 0, 0.0f);
    push(&h, 2.0f, 12.0f, 2 * GEVR_TICK_NS, 0, 0.0f);
    gevrBodyInterp(&h, 0.5f, &s);
    CHECK(near(s.pos[0], 1.0, 1e-5) && near(s.yaw_deg, 11.0, 1e-4), "half way: x %.3f yaw %.3f", s.pos[0], s.yaw_deg);

    push(&h, 500.0f, 12.0f, 3 * GEVR_TICK_NS, 0, 0.0f);   /* a teleport */
    gevrBodyInterp(&h, 0.0f, &s);
    CHECK(h.cut && near(s.pos[0], 500.0, 1e-4), "a jump is a cut: x %.3f", s.pos[0]);

    push(&h, 502.0f, 42.0f, 4 * GEVR_TICK_NS, 0, 0.0f);   /* a snap turn of 30 */
    gevrBodyInterp(&h, 0.0f, &s);
    CHECK(near(s.yaw_deg, 42.0, 1e-4) && near(s.pos[0], 500.0, 1e-4), "a snap stays a snap; position still eased");

    push(&h, 504.0f, 42.0f, 4 * GEVR_TICK_NS + 5 * GEVR_TICK_NS, 0, 0.0f);   /* a gap of 5 ticks */
    gevrBodyInterp(&h, 0.0f, &s);
    CHECK(h.cut && near(s.pos[0], 504.0, 1e-4), "a gap in the ticks is a cut");

    push(&h, 506.0f, 42.0f, 10 * GEVR_TICK_NS, 1, 0.0f);  /* the caller's cut (stereo on, recentre) */
    gevrBodyInterp(&h, 0.0f, &s);
    CHECK(h.cut && near(s.pos[0], 506.0, 1e-4), "an explicit cut");

    /* wrap: 359 -> 1 is two degrees, not 358 */
    push(&h, 506.0f, 359.0f, 11 * GEVR_TICK_NS, 1, 0.0f);
    push(&h, 506.0f, 1.0f, 12 * GEVR_TICK_NS, 0, 0.0f);
    gevrBodyInterp(&h, 0.5f, &s);
    CHECK(near(gevrWrapDelta180(s.yaw_deg - 0.0f), 0.0, 1e-3), "yaw across 0: %.3f", s.yaw_deg);

    /* the head's own walking is shown at once, only the stick's is eased */
    push(&h, 600.0f, 0.0f, 13 * GEVR_TICK_NS, 1, 0.0f);
    push(&h, 605.0f, 0.0f, 14 * GEVR_TICK_NS, 0, 3.0f);   /* 3 of the 5 by the head */
    gevrBodyInterp(&h, 0.0f, &s);
    CHECK(near(s.pos[0], 603.0, 1e-4), "head walk not delayed: x %.3f, want 603", s.pos[0]);
    gevrBodyInterp(&h, 1.0f, &s);
    CHECK(near(s.pos[0], 605.0, 1e-4), "f = 1 is the tick");
}

/* ---- smooth stairs ---------------------------------------------------------------- */

#define FRAME_S (1.0f / 90.0f)

/* the game's own step down: ballistic at 0.27778 units/tick^2 to the new floor */
typedef struct Phys { float floor, vel; } Phys;
static void phys_tick(Phys *p, float stan)
{
    if (stan > p->floor) {   /* step up: the game's IIR 0.83 */
        p->floor = p->floor * 0.83f + stan * 0.17f;
        if (stan - p->floor < 0.01f) p->floor = stan;
        p->vel = 0.0f;
    } else if (stan < p->floor) {
        const float nv = p->vel - 0.27777779f;
        p->floor += (p->vel + nv) * 0.5f;
        p->vel = nv;
        if (p->floor <= stan) { p->floor = stan; p->vel = 0.0f; }
    }
}

/* runs the follow over a step of `rise` at t = 0; returns the time to 90%, the overshoot */
static void stair_step(float rise, float *t90, float *over, float *maxStepPerFrame)
{
    GevrStairFollow s;
    Phys ph = { 0.0f, 0.0f };
    float t = 0.0f, last = 0.0f;
    int frame, tick = -1;

    gevrStairFollowReset(&s);
    gevrStairFollowStep(&s, 0.0f, 0.0f, 0.0f, FRAME_S);
    *t90 = -1.0f;
    *over = 0.0f;
    *maxStepPerFrame = 0.0f;
    last = s.h;
    for (frame = 0; frame < 90; frame++, t += FRAME_S) {
        const int nowTick = (int)(t * 60.0f);
        float h;
        while (tick < nowTick) { tick++; phys_tick(&ph, rise); }
        h = gevrStairFollowStep(&s, ph.floor, rise, ph.vel * 60.0f, FRAME_S);
        if (*t90 < 0.0f && fabsf(h) >= 0.9f * fabsf(rise)) *t90 = t;
        if (rise > 0.0f && h - rise > *over) *over = h - rise;
        if (rise < 0.0f && rise - h > *over) *over = rise - h;
        if (fabsf(h - last) > *maxStepPerFrame) *maxStepPerFrame = fabsf(h - last);
        last = h;
    }
}

static void test_stairs(void)
{
    float upT, upOver, upMax, dnT, dnOver, dnMax;

    stair_step(20.0f, &upT, &upOver, &upMax);
    stair_step(-20.0f, &dnT, &dnOver, &dnMax);
    printf("  stairs: up 90%% in %.0f ms (max %.2f/frame), down in %.0f ms (max %.2f/frame)\n",
           upT * 1000.0f, upMax, dnT * 1000.0f, dnMax);
    CHECK(upT >= 0.08f && upT <= 0.16f, "step up 90%% in %.0f ms, want 100-150", upT * 1000.0f);
    CHECK(dnT >= 0.08f && dnT <= 0.16f, "step down 90%% in %.0f ms, want 100-150", dnT * 1000.0f);
    CHECK(fabsf(upT - dnT) <= 2.0f * FRAME_S, "up and down alike: %.0f vs %.0f ms", upT * 1000.0f, dnT * 1000.0f);
    CHECK(upOver < 0.01f && dnOver < 0.01f, "no overshoot: %.3f %.3f", upOver, dnOver);
    CHECK(upMax < 5.0f && dnMax < 5.0f, "no jump in a frame: %.2f %.2f", upMax, dnMax);

    {
        /* a real drop (2 m): tracked promptly, and no dip on landing */
        GevrStairFollow s;
        Phys ph = { 0.0f, 0.0f };
        float t = 0.0f, worstLag = 0.0f, below = 0.0f, landed = -1.0f, settled = -1.0f;
        int frame, tick = -1, prompt = 0;

        gevrStairFollowReset(&s);
        gevrStairFollowStep(&s, 0.0f, 0.0f, 0.0f, FRAME_S);
        for (frame = 0; frame < 180; frame++, t += FRAME_S) {
            const int nowTick = (int)(t * 60.0f);
            float h;
            while (tick < nowTick) { tick++; phys_tick(&ph, -200.0f); }
            h = gevrStairFollowStep(&s, ph.floor, -200.0f, ph.vel * 60.0f, FRAME_S);
            if (s.mode == GEVR_STAIRS_PROMPT) prompt++;
            if (t > 0.1f && ph.vel != 0.0f && fabsf(h - ph.floor) > worstLag) worstLag = fabsf(h - ph.floor);
            if (ph.floor == -200.0f && landed < 0.0f) landed = t;
            if (landed >= 0.0f && settled < 0.0f && fabsf(h + 200.0f) < 0.5f) settled = t;
            if (-200.0f - h > below) below = -200.0f - h;
        }
        CHECK(prompt > 0, "a 2 m drop is followed promptly");
        CHECK(worstLag < 2.0f, "drop: the camera within %.2f of the falling body after 100 ms", worstLag);
        CHECK(settled >= 0.0f && settled - landed <= 0.15f, "landing settled %.0f ms after the body",
              (settled - landed) * 1000.0f);
        CHECK(below < 0.01f, "landing: no dip below the floor (%.3f)", below);
        CHECK(near(s.h, -200.0, 0.01), "settled on the floor: %.3f", s.h);
    }
    {
        /* resting above the floor's height (vel 0, e.g. on a prop): stays with the body */
        GevrStairFollow s;
        int i;
        gevrStairFollowReset(&s);
        for (i = 0; i < 60; i++) gevrStairFollowStep(&s, 30.0f, 0.0f, 0.0f, FRAME_S);
        CHECK(near(s.h, 30.0, 0.01), "resting above stan stays at the feet: %.3f", s.h);
    }
    {
        /* a long frame gap resets to the body */
        GevrStairFollow s;
        gevrStairFollowReset(&s);
        gevrStairFollowStep(&s, 0.0f, 0.0f, 0.0f, FRAME_S);
        CHECK(gevrStairFollowStep(&s, 50.0f, 50.0f, 0.0f, 0.5f) == 50.0f, "a stall resets the follow");
    }
}

/* ---- sway and bob ----------------------------------------------------------------- */

static void test_sway(void)
{
    GevrSway s;
    float out[3], maxLat = 0.0f, lastLat = 0.0f, lastCross = -1.0f, cross[8];
    int i, ncross = 0;
    const float speed = 200.0f;   /* units/s */

    /* strength 0: nothing at all */
    gevrSwayReset(&s);
    for (i = 0; i < 200; i++) {
        const float xz[2] = { 0.0f, speed * i * FRAME_S };
        gevrSwayStep(&s, xz, 0.0f, 1, 0.0f, FRAME_S, out);
        CHECK(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f, "strength 0 must be zero");
        if (out[0] != 0.0f) break;
    }

    /* walking along +z at full strength: side to side in x, a cycle per two steps */
    gevrSwayReset(&s);
    for (i = 0; i < 400; i++) {
        const float z = speed * i * FRAME_S;
        const float xz[2] = { 0.0f, z };
        gevrSwayStep(&s, xz, 0.0f, 1, 1.0f, FRAME_S, out);
        if (i > 90 && fabsf(out[0]) > maxLat) maxLat = fabsf(out[0]);
        CHECK(fabsf(out[2]) < 1e-3f, "sway is sideways, not along the walk");
        CHECK(out[1] == 0.0f, "no bob on the flat");
        if (i > 90 && ((lastLat < 0.0f) != (out[0] < 0.0f)) && ncross < 8) {
            cross[ncross++] = s.dist;
            lastCross = s.dist;
        }
        lastLat = out[0];
    }
    (void)lastCross;
    CHECK(maxLat > 1.3f && maxLat <= GEVR_SWAY_AMPL_CM + 1e-4f, "full sway %.3f cm, want ~%.1f", maxLat, GEVR_SWAY_AMPL_CM);
    CHECK(ncross >= 3 && near(cross[2] - cross[1], GEVR_SWAY_STEP_CM, 3.0), "a side change per step: %.1f units apart",
          ncross >= 3 ? cross[2] - cross[1] : -1.0f);

    /* stop: it settles to exactly zero and stays */
    {
        const float xz[2] = { 0.0f, speed * 399 * FRAME_S };
        for (i = 0; i < 300; i++) gevrSwayStep(&s, xz, 0.0f, 1, 1.0f, FRAME_S, out);
        CHECK(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f, "standing still: %.5f %.5f", out[0], out[1]);
        for (i = 0; i < 10; i++) {
            gevrSwayStep(&s, xz, 0.0f, 1, 1.0f, FRAME_S, out);
            CHECK(out[0] == 0.0f && out[1] == 0.0f, "stays zero while still");
        }
    }

    /* stairs: a bob while climbing, none while walking a slope that is not stairs mode */
    {
        float maxBob = 0.0f, maxBobOff = 0.0f;
        gevrSwayReset(&s);
        for (i = 0; i < 300; i++) {
            const float xz[2] = { 0.0f, 100.0f * i * FRAME_S };
            gevrSwayStep(&s, xz, 40.0f * i * FRAME_S, 1, 1.0f, FRAME_S, out);
            if (-out[1] > maxBob) maxBob = -out[1];
            CHECK(out[1] <= 0.0f, "the bob dips, never lifts");
        }
        gevrSwayReset(&s);
        for (i = 0; i < 300; i++) {
            const float xz[2] = { 0.0f, 100.0f * i * FRAME_S };
            gevrSwayStep(&s, xz, 40.0f * i * FRAME_S, 0, 1.0f, FRAME_S, out);
            if (fabsf(out[1]) > maxBobOff) maxBobOff = fabsf(out[1]);
        }
        CHECK(maxBob > 0.8f && maxBob <= GEVR_BOB_AMPL_CM + 1e-4f, "stair bob %.3f cm", maxBob);
        CHECK(maxBobOff == 0.0f, "no bob off the stairs follow (falls, lifts): %.3f", maxBobOff);
    }
}

/* ---- the world draws' move --------------------------------------------------------- */

static void mat3_mul(const float a[9], const float b[9], float o[9])
{
    int r, c, k;
    for (r = 0; r < 3; r++) for (c = 0; c < 3; c++) {
        float v = 0.0f;
        for (k = 0; k < 3; k++) v += a[r * 3 + k] * b[k * 3 + c];
        o[r * 3 + c] = v;
    }
}

static void mat3_T(const float a[9], float o[9])
{
    int r, c;
    for (r = 0; r < 3; r++) for (c = 0; c < 3; c++) o[c * 3 + r] = a[r * 3 + c];
}

static void mat3_vec(const float a[9], const float v[3], float o[3])
{
    int r;
    for (r = 0; r < 3; r++) o[r] = a[r * 3] * v[0] + a[r * 3 + 1] * v[1] + a[r * 3 + 2] * v[2];
}

/* bondview2.c gevrRotateByQuat's formula, the oracle for the body yaw's direction */
static void quat_rotate(const float q[4], const float v[3], float o[3])
{
    const float qx = q[0], qy = q[1], qz = q[2], qw = q[3];
    const float t0 = qw * v[0] + qy * v[2] - qz * v[1];
    const float t1 = qw * v[1] + qz * v[0] - qx * v[2];
    const float t2 = qw * v[2] + qx * v[1] - qy * v[0];
    const float t3 = -qx * v[0] - qy * v[1] - qz * v[2];
    o[0] = t0 * qw - t3 * qx - t1 * qz + t2 * qy;
    o[1] = t1 * qw - t3 * qy - t2 * qx + t0 * qz;
    o[2] = t2 * qw - t3 * qz - t0 * qy + t1 * qx;
}

static void test_delta_matrix(void)
{
    float R[9];
    const float yaw = 37.0f;
    const float half = -yaw * (3.14159265f / 180.0f) * 0.5f;
    const float q[4] = { 0.0f, sinf(half), 0.0f, cosf(half) };
    const float vs[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0.3f, -0.5f, 0.8f } };
    int i, k;

    gevrBodyYawMatrix(yaw, R);
    for (i = 0; i < 3; i++) {
        float a[3], b[3];
        quat_rotate(q, vs[i], a);
        mat3_vec(R, vs[i], b);
        CHECK(near(a[0], b[0], 1e-5) && near(a[1], b[1], 1e-5) && near(a[2], b[2], 1e-5),
              "body yaw matrix matches gevrStereoLook's quaternion");
    }

    for (k = 0; k < 4; k++) {
        /* a camera: body yaw x some head rotation, at Ck; shown at Ci with another yaw */
        const float yawK = 20.0f + 50.0f * k, yawI = yawK + (k == 3 ? 0.0f : 1.5f - k);
        const float scale = k == 1 ? 0.2f : 1.0f;
        const float Ck[3] = { 100.0f, 50.0f + k, -30.0f };
        const float dpos[3] = { 1.5f - k, 0.7f * k, -2.0f };
        const float Ci[3] = { Ck[0] + dpos[0], Ck[1] + dpos[1], Ck[2] + dpos[2] };
        float H[9], Rk[9], Ri[9], Ok[9], Oi[9], A[9], OiT[9], M[16];
        const float w[3] = { 140.0f, 20.0f, 60.0f - 10.0f * k };   /* a world point */
        float d[3], ck[3], ci[3], got[3];
        {
            /* head: pitch 20, roll -10 */
            const float p = 0.35f, rl = -0.17f;
            const float Px[9] = { 1, 0, 0, 0, cosf(p), -sinf(p), 0, sinf(p), cosf(p) };
            const float Pz[9] = { cosf(rl), -sinf(rl), 0, sinf(rl), cosf(rl), 0, 0, 0, 1 };
            mat3_mul(Px, Pz, H);
        }
        gevrBodyYawMatrix(yawK, Rk);
        gevrBodyYawMatrix(yawI, Ri);
        mat3_mul(Rk, H, Ok);
        mat3_mul(Ri, H, Oi);
        mat3_T(Ok, A);
        mat3_T(Oi, OiT);
        for (i = 0; i < 3; i++) d[i] = w[i] - Ck[i];
        mat3_vec(A, d, ck);
        for (i = 0; i < 3; i++) ck[i] *= scale;
        for (i = 0; i < 3; i++) d[i] = w[i] - Ci[i];
        mat3_vec(OiT, d, ci);
        for (i = 0; i < 3; i++) ci[i] *= scale;

        gevrBodyDeltaMatrix(A, scale, dpos, gevrWrapDelta180(yawI - yawK), M);
        for (i = 0; i < 3; i++) got[i] = M[i] * ck[0] + M[4 + i] * ck[1] + M[8 + i] * ck[2] + M[12 + i];
        CHECK(near(got[0], ci[0], 1e-3) && near(got[1], ci[1], 1e-3) && near(got[2], ci[2], 1e-3),
              "case %d: moved point (%.4f %.4f %.4f), want (%.4f %.4f %.4f)", k, got[0], got[1], got[2], ci[0], ci[1], ci[2]);
    }

    {
        float A[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, M[16], I[16], P[16];
        const float z[3] = { 0, 0, 0 };
        gevrBodyDeltaMatrix(A, 1.0f, z, 0.0f, M);
        for (i = 0; i < 16; i++) CHECK(near(M[i], (i % 5) == 0 ? 1.0 : 0.0, 1e-6), "no move is the identity");
        memcpy(I, M, sizeof(I));
        gevrMat4Mul(I, M, P);
        for (i = 0; i < 16; i++) CHECK(near(P[i], I[i], 1e-6), "identity product");
    }
}

/* ---- even gait --------------------------------------------------------------------- */

static void test_gait(void)
{
    /* the walk loop: 17.5 frames, root motion per frame surging with the step */
    const float loop = 17.5f, perTick = 0.6f;
    GevrGaitLearner g;
    double rawTotal = 0.0, evenTotal = 0.0, frame = 0.0;
    float firstEven = -1.0f, maxDiff = 0.0f;
    int t;

    gevrGaitReset(&g);
    CHECK(gevrGaitEven(&g, 3.0f, perTick) == 3.0f, "before a whole loop: the animation's own value");
    for (t = 0; t < 2000; t++) {
        /* integral of 1 + 0.6 sin(2 pi f / loop) over this tick's frames */
        const double f0 = frame, f1 = frame + perTick;
        const double w = 2.0 * 3.14159265358979 / loop;
        const float disp = (float)((f1 - f0) - 0.6 / w * (cos(w * f1) - cos(w * f0)));
        float even;
        frame = f1;
        gevrGaitObserve(&g, disp, perTick, loop);
        even = gevrGaitEven(&g, disp, perTick);
        if (t >= 200) {
            rawTotal += disp;
            evenTotal += even;
            if (firstEven < 0.0f) firstEven = even;
            if (fabsf(even - firstEven) > maxDiff) maxDiff = fabsf(even - firstEven);
        }
    }
    printf("  gait: ratio %.4f per frame (true 1.0), even/raw distance %.5f\n", g.ratio, evenTotal / rawTotal);
    CHECK(near(g.ratio, 1.0, 0.005), "learned mean %.4f per frame, want 1.0", g.ratio);
    CHECK(near(evenTotal / rawTotal, 1.0, 0.005), "same distance: even/raw %.5f", evenTotal / rawTotal);
    CHECK(maxDiff < 0.005f, "an even speed: per-tick spread %.4f", maxDiff);
    gevrGaitObserve(&g, 1.0f, 0.0f, loop);   /* standing: no frames, nothing learned */
    CHECK(gevrGaitEven(&g, 5.0f, 0.0f) == 0.0f, "no frames, no move");
}

static void test_stats(void)
{
    GevrStepStats s;
    gevrStepStatsReset(&s);
    gevrStepStatsAdd(&s, 1.0, 0.2f);
    gevrStepStatsAdd(&s, 3.0, 0.8f);
    CHECK(near(gevrStepStatsMean(&s), 2.0, 1e-9) && near(gevrStepStatsStddev(&s), 1.0, 1e-9) && s.max == 3.0,
          "stats mean/sd/max");
    CHECK(s.fmin == 0.2f && s.fmax == 0.8f, "fraction range");
}

int main(void)
{
    test_tick_clock();
    test_even_walk();
    test_cuts();
    test_stairs();
    test_sway();
    test_delta_matrix();
    test_gait();
    test_stats();
    printf("test_vr_motion: %d checks, %d failed\n", s_checks, s_fail);
    return s_fail ? 1 : 0;
}
