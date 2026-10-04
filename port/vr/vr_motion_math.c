/* See vr_motion_math.h. Pure: host-tested by tools/steam_frame/tests/test_vr_motion.c. */
#include "vr_motion_math.h"

#include <math.h>
#include <string.h>

#define GEVR_PI 3.14159265358979323846f

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

/* ---- the tick clock --------------------------------------------------------- */

void gevrTickClockInit(GevrTickClock *c, int64_t period_ns)
{
    memset(c, 0, sizeof(*c));
    c->period_ns = period_ns > 0 ? period_ns : GEVR_TICK_NS;
}

int gevrTickClockDue(GevrTickClock *c, int64_t display_ns)
{
    if (c->next_ns == 0
        || display_ns - c->next_ns > 100000000LL            /* stalled: never fast-forward the game */
        || c->next_ns - display_ns > 3 * c->period_ns) {     /* the clock went back */
        c->next_ns = display_ns;
        c->cut = 1;
    }
    /* half a millisecond of slack: a frame landing a hair before the tick runs it */
    return display_ns + 500000LL >= c->next_ns;
}

void gevrTickClockTicked(GevrTickClock *c)
{
    c->cur_ns = c->next_ns;
    c->next_ns += c->period_ns;
}

float gevrTickFraction(int64_t display_ns, int64_t tick_ns, int64_t period_ns)
{
    if (period_ns <= 0) {
        return 1.0f;
    }
    return clampf((float)((double)(display_ns - tick_ns) / (double)period_ns), 0.0f, 1.0f);
}

/* ---- the body's ticks --------------------------------------------------------- */

float gevrWrapDelta180(float deg)
{
    while (deg > 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}

void gevrBodyReset(GevrBodyHistory *h)
{
    memset(h, 0, sizeof(*h));
}

void gevrBodyPush(GevrBodyHistory *h, const GevrBodyPose *p, int cut)
{
    if (h->have && !cut) {
        const float dx = p->pos[0] - h->cur.pos[0];
        const float dy = p->pos[1] - h->cur.pos[1];
        const float dz = p->pos[2] - h->cur.pos[2];
        const int64_t dt = p->t_ns - h->cur.t_ns;

        if (dx * dx + dy * dy + dz * dz > GEVR_BODY_JUMP_CUT * GEVR_BODY_JUMP_CUT
            || dt <= 0 || dt > (5 * GEVR_TICK_NS) / 2) {
            cut = 1;
        }
    }
    if (!h->have || cut) {
        h->prev = *p;
        h->cur = *p;
        h->prev.head_walk[0] = h->prev.head_walk[1] = 0.0f;
        h->cur.head_walk[0] = h->cur.head_walk[1] = 0.0f;
        h->have = 1;
        h->cut = 1;
        return;
    }
    h->prev = h->cur;
    h->cur = *p;
    h->cut = 0;
    if (fabsf(gevrWrapDelta180(p->yaw_deg - h->prev.yaw_deg)) > GEVR_BODY_YAW_CUT) {
        h->prev.yaw_deg = p->yaw_deg;   /* a snap turn stays a snap */
    }
}

void gevrBodyInterp(const GevrBodyHistory *h, float f, GevrBodyShown *out)
{
    const GevrBodyPose *a = &h->prev, *b = &h->cur;
    const float g = 1.0f - clampf(f, 0.0f, 1.0f);

    /* back from the latest tick by what is still to come of its stick motion */
    out->pos[0] = b->pos[0] - g * (b->pos[0] - a->pos[0] - b->head_walk[0]);
    out->pos[1] = b->pos[1];
    out->pos[2] = b->pos[2] - g * (b->pos[2] - a->pos[2] - b->head_walk[1]);
    out->yaw_deg = b->yaw_deg - g * gevrWrapDelta180(b->yaw_deg - a->yaw_deg);
    out->floor_y = b->floor_y - g * (b->floor_y - a->floor_y);
    out->stan_y = b->stan_y - g * (b->stan_y - a->stan_y);
    out->vel_y = b->vel_y - g * (b->vel_y - a->vel_y);
}

/* ---- smooth stairs ---------------------------------------------------------------- */

void gevrStairFollowReset(GevrStairFollow *s)
{
    memset(s, 0, sizeof(*s));
}

float gevrStairFollowStep(GevrStairFollow *s, float floor_y, float stan_y, float vel_y_per_s, float dt_s)
{
    const float gap = floor_y - stan_y;   /* > 0: the feet above the floor (falling to it) */
    const int falling = vel_y_per_s < -GEVR_STAIRS_FALL;
    int prompt;

    if (!s->valid || !(dt_s > 0.0f) || dt_s > 0.1f) {
        s->h = floor_y;
        s->v = 0.0f;
        s->off = 0.0f;
        s->mode = GEVR_STAIRS_SMOOTH;
        s->valid = 1;
        return s->h;
    }

    prompt = gap > GEVR_STAIRS_BIG || gap < -GEVR_STAIRS_BIG || (falling && gap > 0.5f);

    if (prompt) {
        /* a drop, a fall or a lift: follow the body, closing any gap quickly */
        const float h0 = s->h;
        if (s->mode != GEVR_STAIRS_PROMPT) {
            s->off = s->h - floor_y;
            s->mode = GEVR_STAIRS_PROMPT;
        }
        s->off *= expf(-dt_s / GEVR_STAIRS_PROMPT_TAU);
        s->h = floor_y + s->off;
        s->v = (s->h - h0) / dt_s;
        return s->h;
    }

    if (s->mode == GEVR_STAIRS_PROMPT) {
        /* carry on at no more than a stair's pace: no stop and start at the hand-over */
        s->mode = GEVR_STAIRS_SMOOTH;
        s->v = clampf(s->v, -GEVR_STAIRS_FALL, GEVR_STAIRS_FALL);
    }

    {
        /* Small steps: ease to the floor the feet are going to, up or down alike.
         * Resting above the floor (vel 0) is where the body is: stay there. */
        const float target = (stan_y > floor_y || vel_y_per_s < -0.01f) ? stan_y : floor_y;
        const float w = GEVR_STAIRS_OMEGA;
        const float e = s->h - target;
        const float a = s->v + w * e;
        const float k = expf(-w * dt_s);

        s->h = target + (e + a * dt_s) * k;
        s->v = (s->v - w * a * dt_s) * k;
        /* arriving with speed (a landing) stops at the floor: never a dip past it */
        if ((e >= 0.0f && s->h < target) || (e <= 0.0f && s->h > target)) {
            s->h = target;
            s->v = 0.0f;
        }
    }
    return s->h;
}

/* ---- experimental sway and bob ------------------------------------------------------ */

void gevrSwayReset(GevrSway *s)
{
    memset(s, 0, sizeof(*s));
}

void gevrSwayStep(GevrSway *s, const float xz[2], float h, int on_stairs, float strength, float dt_s, float out[3])
{
    float dx, dz, ds, dh, target, k;

    out[0] = out[1] = out[2] = 0.0f;
    if (!(strength > 0.0f)) {
        gevrSwayReset(s);
        return;
    }
    if (!s->valid || !(dt_s > 0.0f) || dt_s > 0.1f) {
        const float keepDir[2] = { s->dir[0], s->dir[1] };
        gevrSwayReset(s);
        s->last_xz[0] = xz[0];
        s->last_xz[1] = xz[1];
        s->last_h = h;
        s->dir[0] = keepDir[0];
        s->dir[1] = keepDir[1];
        if (s->dir[0] == 0.0f && s->dir[1] == 0.0f) s->dir[1] = 1.0f;
        s->valid = 1;
        return;
    }
    if (strength > 1.0f) strength = 1.0f;

    dx = xz[0] - s->last_xz[0];
    dz = xz[1] - s->last_xz[1];
    dh = h - s->last_h;
    s->last_xz[0] = xz[0];
    s->last_xz[1] = xz[1];
    s->last_h = h;
    ds = sqrtf(dx * dx + dz * dz);

    if (ds > 1e-4f) {
        float n;
        k = 1.0f - expf(-dt_s / 0.15f);
        s->dir[0] += (dx / ds - s->dir[0]) * k;
        s->dir[1] += (dz / ds - s->dir[1]) * k;
        n = sqrtf(s->dir[0] * s->dir[0] + s->dir[1] * s->dir[1]);
        if (n > 1e-4f) {
            s->dir[0] /= n;
            s->dir[1] /= n;
        }
    }

    /* side to side: one cycle per two steps of distance */
    s->dist += ds;
    if (s->dist > 1.0e6f) s->dist = fmodf(s->dist, 2.0f * GEVR_SWAY_STEP_CM);
    target = clampf(ds / dt_s / GEVR_SWAY_FULL_CMS, 0.0f, 1.0f);
    s->env += (target - s->env) * (1.0f - expf(-dt_s / 0.25f));
    if (target == 0.0f && s->env < 1e-3f) s->env = 0.0f;
    {
        const float lat = strength * GEVR_SWAY_AMPL_CM * s->env * sinf(GEVR_PI * s->dist / GEVR_SWAY_STEP_CM);
        out[0] = s->dir[1] * lat;     /* the travel direction's perpendicular */
        out[2] = -s->dir[0] * lat;
    }

    /* the stair bob: a dip per stair's worth of height, only while walking on stairs */
    target = 0.0f;
    if (on_stairs && ds > 1e-4f) {
        s->vdist += fabsf(dh);
        if (s->vdist > 1.0e6f) s->vdist = fmodf(s->vdist, GEVR_BOB_RISE_CM);
        target = clampf(fabsf(dh) / dt_s / GEVR_BOB_FULL_CMS, 0.0f, 1.0f);
    }
    s->venv += (target - s->venv) * (1.0f - expf(-dt_s / 0.15f));
    if (target == 0.0f && s->venv < 1e-3f) s->venv = 0.0f;
    out[1] = -strength * GEVR_BOB_AMPL_CM * s->venv
           * 0.5f * (1.0f - cosf(2.0f * GEVR_PI * s->vdist / GEVR_BOB_RISE_CM));
}

/* ---- the world draws' move -------------------------------------------------------- */

void gevrBodyYawMatrix(float yaw_deg, float R[9])
{
    /* a rotation of -yaw about +Y, as gevrRotateByQuat turns by (0, sin(-yaw/2), 0, cos(-yaw/2)) */
    const float a = -yaw_deg * (GEVR_PI / 180.0f);
    const float c = cosf(a), s = sinf(a);

    R[0] = c;    R[1] = 0.0f; R[2] = s;
    R[3] = 0.0f; R[4] = 1.0f; R[5] = 0.0f;
    R[6] = -s;   R[7] = 0.0f; R[8] = c;
}

void gevrBodyDeltaMatrix(const float A[9], float scale, const float dpos[3], float dyaw_deg, float out[16])
{
    float Q[9], AQ[9], M[9], t[3];
    int r, c, k;

    /* Q = Rb(tick) Rb(shown)^T = Rb(-(shown - tick)): rotations about one axis */
    gevrBodyYawMatrix(-dyaw_deg, Q);
    for (r = 0; r < 3; r++) {
        for (c = 0; c < 3; c++) {
            float v = 0.0f;
            for (k = 0; k < 3; k++) v += A[r * 3 + k] * Q[k * 3 + c];
            AQ[r * 3 + c] = v;
        }
    }
    for (r = 0; r < 3; r++) {
        for (c = 0; c < 3; c++) {
            float v = 0.0f;
            for (k = 0; k < 3; k++) v += AQ[r * 3 + k] * A[c * 3 + k];   /* (AQ) A^T */
            M[r * 3 + c] = v;
        }
        t[r] = -scale * (AQ[r * 3 + 0] * dpos[0] + AQ[r * 3 + 1] * dpos[1] + AQ[r * 3 + 2] * dpos[2]);
    }
    for (r = 0; r < 3; r++) {
        for (c = 0; c < 3; c++) {
            out[c * 4 + r] = M[r * 3 + c];
        }
        out[12 + r] = t[r];
        out[r * 4 + 3] = 0.0f;
    }
    out[15] = 1.0f;
}

void gevrMat4Mul(const float a[16], const float b[16], float out[16])
{
    float o[16];
    int r, c, k;

    for (c = 0; c < 4; c++) {
        for (r = 0; r < 4; r++) {
            float v = 0.0f;
            for (k = 0; k < 4; k++) v += a[k * 4 + r] * b[c * 4 + k];
            o[c * 4 + r] = v;
        }
    }
    memcpy(out, o, sizeof(o));
}

/* ---- even walking speed --------------------------------------------------------- */

void gevrGaitReset(GevrGaitLearner *g)
{
    memset(g, 0, sizeof(*g));
}

void gevrGaitObserve(GevrGaitLearner *g, float disp, float frames, float loop_len)
{
    if (!(frames > 0.0f) || !(loop_len > 0.0f) || frames > loop_len) {
        return;
    }
    if (g->sum_frames + frames < loop_len) {
        g->sum_disp += disp;
        g->sum_frames += frames;
        return;
    }
    {
        /* this tick closes the loop: only the part of it inside the loop counts */
        const float part = (loop_len - g->sum_frames) / frames;
        const float r = (g->sum_disp + disp * part) / loop_len;

        g->ratio = g->loops > 0 ? (g->ratio * (float)g->loops + r) / (float)(g->loops + 1) : r;
        if (g->loops < 8) g->loops++;
        g->sum_disp = disp * (1.0f - part);
        g->sum_frames = frames * (1.0f - part);
    }
}

float gevrGaitEven(const GevrGaitLearner *g, float disp, float frames)
{
    return g->loops > 0 ? g->ratio * frames : disp;
}

/* ---- [vr-move] ----------------------------------------------------------------------- */

void gevrStepStatsReset(GevrStepStats *s)
{
    memset(s, 0, sizeof(*s));
    s->fmin = 1.0f;
    s->fmax = 0.0f;
}

void gevrStepStatsAdd(GevrStepStats *s, double step, float f)
{
    s->n++;
    s->sum += step;
    s->sumsq += step * step;
    if (step > s->max) s->max = step;
    if (f < s->fmin) s->fmin = f;
    if (f > s->fmax) s->fmax = f;
}

double gevrStepStatsMean(const GevrStepStats *s)
{
    return s->n > 0 ? s->sum / s->n : 0.0;
}

double gevrStepStatsStddev(const GevrStepStats *s)
{
    double m, v;
    if (s->n < 2) return 0.0;
    m = s->sum / s->n;
    v = s->sumsq / s->n - m * m;
    return v > 0.0 ? sqrt(v) : 0.0;
}
