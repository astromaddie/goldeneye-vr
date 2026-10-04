/*
 * The paused watch's screen placement (see vr_screen_math.h).
 *
 * The game links its own atan2f, sinf and cosf into this library (src/game, N64 ranges), and
 * they replace libm's for the VR code too: only the double versions are used here.
 */
#include <math.h>

#include "vr_screen_math.h"

void gevrScreenPlaceAhead(const float head[3], const float headQuat[4], float dist, float width,
                          float drop, int curved, GevrScreenPlace *out)
{
    const double x = headQuat[0], y = headQuat[1], z = headQuat[2], w = headQuat[3];
    /* the head's -Z (where it looks) and +Y, rotated into play space */
    double fx = -2.0 * (x * z + w * y);
    double fy = -2.0 * (y * z - w * x);
    double fz = -(1.0 - 2.0 * (x * x + y * y));
    double len = sqrt(fx * fx + fz * fz);
    double yaw, half;

    if (len < 0.05) {
        /* straight up or down: the top of the head points where the face would */
        const double ux = 2.0 * (x * y - w * z);
        const double uz = 2.0 * (y * z + w * x);
        const double s = fy < 0.0 ? 1.0 : -1.0;
        fx = ux * s;
        fz = uz * s;
        len = sqrt(fx * fx + fz * fz);
        if (len < 1e-6) {
            fx = 0.0;
            fz = -1.0;
            len = 1.0;
        }
    }
    fx /= len;
    fz /= len;

    out->pos[0] = head[0] + (float)fx * dist;
    out->pos[1] = head[1] - drop;
    out->pos[2] = head[2] + (float)fz * dist;
    /* a quad shows its +Z face: its -Z runs along the view */
    yaw = atan2(-fx, -fz);
    half = yaw * 0.5;
    out->yaw = (float)yaw;
    out->quat[0] = 0.0f;
    out->quat[1] = (float)sin(half);
    out->quat[2] = 0.0f;
    out->quat[3] = (float)cos(half);
    out->dist = dist;
    out->width = width;
    out->curved = curved ? 1 : 0;
    out->fovDeg = (float)((curved ? (double)width / dist : 2.0 * atan((double)width / (2.0 * dist)))
                          * 180.0 / 3.14159265358979323846);
}

int gevrWatchScreenStep(GevrWatchScreenState *s, int watchUp, int recentre)
{
    int place = 0;

    if (watchUp && (!s->up || recentre)) {
        place = 1;
        s->placements++;
    }
    s->up = watchUp ? 1 : 0;
    return place;
}

/* ------------------------------------------------------------ the Q-Branch terminal (option C) */

static const double kTermPi = 3.14159265358979323846;

static double wrap_pi(double a)
{
    while (a > kTermPi) a -= 2.0 * kTermPi;
    while (a <= -kTermPi) a += 2.0 * kTermPi;
    return a;
}

/* Hamilton product, {x, y, z, w} */
static void qmul(const double a[4], const double b[4], double o[4])
{
    o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

/* v rotated by the unit quaternion q */
static void qrot(const double q[4], const double v[3], double o[3])
{
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    o[0] = (1.0 - 2.0 * (y * y + z * z)) * v[0] + 2.0 * (x * y - w * z) * v[1] + 2.0 * (x * z + w * y) * v[2];
    o[1] = 2.0 * (x * y + w * z) * v[0] + (1.0 - 2.0 * (x * x + z * z)) * v[1] + 2.0 * (y * z - w * x) * v[2];
    o[2] = 2.0 * (x * z - w * y) * v[0] + 2.0 * (y * z + w * x) * v[1] + (1.0 - 2.0 * (x * x + y * y)) * v[2];
}

float gevrHeadYaw(const float headQuat[4])
{
    /* the same rule as gevrScreenPlaceAhead: the yaw of a screen placed ahead of this head */
    static const float origin[3] = { 0.0f, 0.0f, 0.0f };
    GevrScreenPlace p;
    gevrScreenPlaceAhead(origin, headQuat, 1.0f, 1.0f, 0.0f, 0, &p);
    return p.yaw;
}

void gevrTermPlace(GevrTermPanel *p, const float head[3], const float headQuat[4])
{
    p->pivot[0] = head[0];
    p->pivot[1] = head[1];
    p->pivot[2] = head[2];
    p->yaw = gevrHeadYaw(headQuat);
    p->following = 0;
    p->placements++;
}

/* yaw about +Y, then `pitch` about the turned +X (negative: the face looks up) */
static void yaw_pitch_quat(double yaw, double turn, double pitch, double out[4])
{
    const double qy[4] = { 0.0, sin((yaw + turn) * 0.5), 0.0, cos((yaw + turn) * 0.5) };
    const double qx[4] = { sin(pitch * 0.5), 0.0, 0.0, cos(pitch * 0.5) };
    qmul(qy, qx, out);
}

void gevrTermPose(const GevrTermPanel *p, float pos[3], float quat[4])
{
    const double yaw = p->yaw;
    double q[4];
    pos[0] = (float)(p->pivot[0] - sin(yaw) * GEVR_TERM_DIST_M);
    pos[1] = p->pivot[1] - GEVR_TERM_DROP_M;
    pos[2] = (float)(p->pivot[2] - cos(yaw) * GEVR_TERM_DIST_M);
    /* the face (+Z) looks back up at the eyes: the bottom edge comes toward the viewer */
    yaw_pitch_quat(yaw, 0.0, -atan2(GEVR_TERM_DROP_M, GEVR_TERM_DIST_M), q);
    for (int i = 0; i < 4; i++) quat[i] = (float)q[i];
}

void gevrTermDockPose(const GevrTermPanel *p, float aspect, float pos[3], float quat[4], float size[2])
{
    const double turn = -GEVR_TERM_DOCK_DEG * kTermPi / 180.0;   /* its face turns toward the viewer */
    const double h = GEVR_TERM_DOCK_HEIGHT_M, w = h * (aspect > 0.1f ? aspect : 4.0 / 3.0);
    const double yq[4] = { 0.0, sin(p->yaw * 0.5), 0.0, cos(p->yaw * 0.5) };
    /* in the terminal's own level frame (+X right, +Z back toward the viewer, origin its centre):
     * hinged a gap off its right edge, its +X running (cos 20, 0, sin 20) */
    const double local[3] = { GEVR_TERM_WIDTH_M * 0.5 + GEVR_TERM_DOCK_GAP_M + 0.5 * w * cos(turn), 0.0,
                              -0.5 * w * sin(turn) };
    double off[3], q[4], c[3];
    qrot(yq, local, off);
    c[0] = p->pivot[0] - sin(p->yaw) * GEVR_TERM_DIST_M + off[0];
    c[1] = p->pivot[1] - GEVR_TERM_DROP_M;
    c[2] = p->pivot[2] - cos(p->yaw) * GEVR_TERM_DIST_M + off[2];
    {
        const double dx = c[0] - p->pivot[0], dz = c[2] - p->pivot[2];
        yaw_pitch_quat(p->yaw, turn, -atan2(GEVR_TERM_DROP_M, sqrt(dx * dx + dz * dz)), q);
    }
    for (int i = 0; i < 3; i++) pos[i] = (float)c[i];
    for (int i = 0; i < 4; i++) quat[i] = (float)q[i];
    size[0] = (float)w;
    size[1] = (float)h;
}

int gevrTermFollow(GevrTermPanel *p, const float head[3], const float headQuat[4], float dt)
{
    const double hy = gevrHeadYaw(headQuat);
    double delta = wrap_pi(hy - p->yaw);

    if (!p->following && fabs(delta) > GEVR_TERM_FOLLOW_DEG * kTermPi / 180.0) {
        p->following = 1;
    }
    if (p->following && dt > 0.0f) {
        const double k = 1.0 - exp(-(double)dt / GEVR_TERM_FOLLOW_TAU_S);
        p->yaw = (float)wrap_pi(p->yaw + delta * k);
        p->pivot[0] += (float)((head[0] - p->pivot[0]) * k);
        p->pivot[2] += (float)((head[2] - p->pivot[2]) * k);
        delta = wrap_pi(hy - p->yaw);
        if (fabs(delta) < GEVR_TERM_SETTLE_DEG * kTermPi / 180.0) {
            p->following = 0;
        }
        return 1;
    }
    return p->following;
}

float gevrTermEase(float s)
{
    const double t = s < 0.0f ? 0.0 : s > 1.0f ? 1.0 : s;
    return (float)(1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t));
}

int gevrQuadHit(const float pos[3], const float quat[4], float w, float h, const float o[3],
                const float d[3], float *u, float *v)
{
    const double q[4] = { quat[0], quat[1], quat[2], quat[3] };
    static const double ex[3] = { 1, 0, 0 }, ey[3] = { 0, 1, 0 }, ez[3] = { 0, 0, 1 };
    double ax[3], ay[3], n[3];
    qrot(q, ex, ax);
    qrot(q, ey, ay);
    qrot(q, ez, n);
    const double denom = d[0] * n[0] + d[1] * n[1] + d[2] * n[2];
    if (denom > -1e-4 || w <= 0.0f || h <= 0.0f) {
        return 0;   /* parallel, or pointing at its back */
    }
    const double t = ((pos[0] - o[0]) * n[0] + (pos[1] - o[1]) * n[1] + (pos[2] - o[2]) * n[2]) / denom;
    if (t <= 0.0) {
        return 0;
    }
    const double hx = o[0] + d[0] * t - pos[0], hy = o[1] + d[1] * t - pos[1], hz = o[2] + d[2] * t - pos[2];
    const double uu = 0.5 + (hx * ax[0] + hy * ax[1] + hz * ax[2]) / w;
    const double vv = 0.5 - (hx * ay[0] + hy * ay[1] + hz * ay[2]) / h;
    if (uu < 0.0 || uu > 1.0 || vv < 0.0 || vv > 1.0) {
        return 0;
    }
    *u = (float)uu;
    *v = (float)vv;
    return 1;
}

void gevrQuadPoint(const float pos[3], const float quat[4], float w, float h, float u, float v, float out[3])
{
    const double q[4] = { quat[0], quat[1], quat[2], quat[3] };
    const double l[3] = { (u - 0.5) * w, (0.5 - v) * h, 0.0 };
    double r[3];
    qrot(q, l, r);
    out[0] = (float)(pos[0] + r[0]);
    out[1] = (float)(pos[1] + r[1]);
    out[2] = (float)(pos[2] + r[2]);
}

int gevrTermCrop(const float box[4], float screenW, float screenH, float aspect, float margin, float out[4])
{
    double x0 = box[0], y0 = box[1], x1 = box[2], y1 = box[3];
    int ok = 1;

    if (!(x1 - x0 >= 0.25 * screenW) || !(y1 - y0 >= 0.25 * screenH)) {   /* also NaN */
        x0 = 0.0;
        y0 = 0.0;
        x1 = screenW;
        y1 = screenH;
        ok = 0;
    } else {
        const double m = margin * ((x1 - x0) > (y1 - y0) ? (x1 - x0) : (y1 - y0));
        x0 -= m;
        y0 -= m;
        x1 += m;
        y1 += m;
    }
    {
        const double cx = 0.5 * (x0 + x1), cy = 0.5 * (y0 + y1);
        double w = x1 - x0, h = y1 - y0;
        if (aspect > 0.0f) {
            if (w / h < aspect) {
                w = h * aspect;
            } else {
                h = w / aspect;
            }
        }
        out[0] = (float)(cx - 0.5 * w);
        out[1] = (float)(cy - 0.5 * h);
        out[2] = (float)w;
        out[3] = (float)h;
    }
    return ok;
}
