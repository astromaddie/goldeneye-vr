/*
 * Host test of the paused watch's screen placement (port/vr/vr_screen_math.c, the production file
 * vr_openxr.cpp gevrVrWatchScreen uses). Run: tools/steam_frame/tests/run_tests.sh
 *
 * Watch option D (2026-10-04): a curved screen, world-locked, 1.4 m wide at 1.5 m, straight ahead
 * at the head's yaw when the watch comes up, placed again on a new pause and on a recentre.
 *  - yaw-only anchor: a head pitched or rolled places the same screen as a level one at that yaw;
 *  - the distance, the drop below eye height and the width (flat chord / curved arc) are kept;
 *  - the screen faces the head (its +Z points back at it);
 *  - looking straight down or up still gives the yaw the face points along;
 *  - placed on the rising edge only, not again while the watch stays up; again on a new pause,
 *    and on a recentre while it is up (never while it is down).
 *
 * Watch option C, the Q-Branch terminal (chosen 2026-10-04 in place of D; vr_watch_terminal.cpp):
 *  - 0.9 m ahead along the head's yaw, 15 cm below the eyes, 60 x 38 cm, its face looking straight
 *    back at the eyes (bottom edge nearer than the top); pitch and roll of the head do not move it;
 *    a seated head places it at a seated height;
 *  - the lazy re-centre: nothing within 50 degrees, then a smooth glide (no frame jumps more than a
 *    few degrees) back to the head's yaw and position that stops within 2 degrees; height kept;
 *  - re-placed on open and on a recentre;
 *  - the docked VR settings panel: right of it, 3 cm off its edge, turned 20 degrees in, 48 cm tall;
 *  - the ray-quad hit (the laser on the dock) and its inverse, back faces and misses;
 *  - the page crop: the face plus a margin at the texture's aspect, and the whole-screen fallback.
 */
#include <math.h>
#include <stdio.h>

#include "vr_screen_math.h"

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", what, __LINE__); failures++; } } while (0)
#define NEAR(a, b, eps) (fabs((double)(a) - (double)(b)) < (eps))

static const double kPi = 3.14159265358979323846;

/* q = yaw about +Y, then pitch about the turned +X, then roll about the turned +Z (Hamilton, {x,y,z,w}) */
static void mul(const double a[4], const double b[4], double o[4])
{
    o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

static void headQuat(double yawDeg, double pitchDeg, double rollDeg, float out[4])
{
    const double hy = yawDeg * kPi / 360.0, hp = pitchDeg * kPi / 360.0, hr = rollDeg * kPi / 360.0;
    const double qy[4] = { 0.0, sin(hy), 0.0, cos(hy) };
    const double qp[4] = { sin(hp), 0.0, 0.0, cos(hp) };
    const double qr[4] = { 0.0, 0.0, sin(hr), cos(hr) };
    double t[4], q[4];
    mul(qy, qp, t);
    mul(t, qr, q);
    for (int i = 0; i < 4; i++) out[i] = (float)q[i];
}

static void testPlacement(void)
{
    const float head[3] = { 0.3f, 1.62f, -0.4f };
    static const double yaws[] = { 0.0, 37.0, 90.0, -120.0, 179.0 };
    static const double tilts[][2] = { { 0, 0 }, { -35, 0 }, { 25, 0 }, { 0, 20 }, { -50, -15 } };

    for (unsigned i = 0; i < sizeof(yaws) / sizeof(yaws[0]); i++) {
        GevrScreenPlace level;
        float q[4];
        headQuat(yaws[i], 0.0, 0.0, q);
        gevrScreenPlaceAhead(head, q, GEVR_WATCH_SCREEN_DIST_M, GEVR_WATCH_SCREEN_WIDTH_M,
                             GEVR_WATCH_SCREEN_DROP_M, 1, &level);

        /* straight ahead along the yaw: OpenXR's yaw turns -Z toward -X */
        const double a = yaws[i] * kPi / 180.0;
        CHECK(NEAR(level.pos[0], head[0] - sin(a) * 1.5, 1e-4), "ahead along the yaw (x)");
        CHECK(NEAR(level.pos[2], head[2] - cos(a) * 1.5, 1e-4), "ahead along the yaw (z)");
        CHECK(NEAR(level.pos[1], head[1] - 0.10, 1e-5), "centre 10 cm below eye height");
        {
            const double dx = level.pos[0] - head[0], dz = level.pos[2] - head[2];
            CHECK(NEAR(sqrt(dx * dx + dz * dz), 1.5, 1e-4), "1.5 m from the head");
        }
        /* faces the head: the quad's +Z (yaw-rotated) points from the screen back to the head */
        {
            const double nx = sin(level.yaw), nz = cos(level.yaw);
            const double bx = head[0] - level.pos[0], bz = head[2] - level.pos[2];
            CHECK(NEAR((nx * bx + nz * bz) / 1.5, 1.0, 1e-4), "faces the head");
            CHECK(NEAR(level.quat[0], 0.0, 1e-7) && NEAR(level.quat[2], 0.0, 1e-7), "yaw-only orientation");
            CHECK(NEAR(level.quat[1] * level.quat[1] + level.quat[3] * level.quat[3], 1.0, 1e-5), "unit quaternion");
        }
        CHECK(NEAR(level.width, 1.4, 1e-6) && NEAR(level.dist, 1.5, 1e-6), "width and distance kept");
        CHECK(NEAR(level.fovDeg, 1.4 / 1.5 * 180.0 / kPi, 1e-3), "curved: the arc spans w/d radians");
        CHECK(level.curved == 1, "curved flag");

        /* the head pitched and rolled: the same screen */
        for (unsigned k = 0; k < sizeof(tilts) / sizeof(tilts[0]); k++) {
            GevrScreenPlace tilted;
            headQuat(yaws[i], tilts[k][0], tilts[k][1], q);
            gevrScreenPlaceAhead(head, q, GEVR_WATCH_SCREEN_DIST_M, GEVR_WATCH_SCREEN_WIDTH_M,
                                 GEVR_WATCH_SCREEN_DROP_M, 1, &tilted);
            CHECK(NEAR(tilted.pos[0], level.pos[0], 1e-3) && NEAR(tilted.pos[1], level.pos[1], 1e-5)
                  && NEAR(tilted.pos[2], level.pos[2], 1e-3), "pitch and roll do not move it");
            CHECK(NEAR(tilted.quat[1], level.quat[1], 1e-3) && NEAR(tilted.quat[3], level.quat[3], 1e-3),
                  "pitch and roll do not turn it");
        }
    }

    /* flat fallback (no cylinder layer): the chord is the width */
    {
        GevrScreenPlace flat;
        float q[4];
        headQuat(0.0, 0.0, 0.0, q);
        gevrScreenPlaceAhead(head, q, 1.5f, 1.4f, 0.10f, 0, &flat);
        CHECK(flat.curved == 0, "flat flag");
        CHECK(NEAR(2.0 * 1.5 * tan(flat.fovDeg * kPi / 360.0), 1.4, 1e-4), "flat: 1.4 m chord at 1.5 m");
    }

    /* straight down and straight up: the yaw the face points along */
    {
        GevrScreenPlace p;
        float q[4];
        headQuat(60.0, -90.0, 0.0, q);
        gevrScreenPlaceAhead(head, q, 1.5f, 1.4f, 0.1f, 1, &p);
        CHECK(NEAR(p.yaw, 60.0 * kPi / 180.0, 1e-3), "looking straight down keeps the yaw");
        headQuat(-30.0, 90.0, 0.0, q);
        gevrScreenPlaceAhead(head, q, 1.5f, 1.4f, 0.1f, 1, &p);
        CHECK(NEAR(p.yaw, -30.0 * kPi / 180.0, 1e-3), "looking straight up keeps the yaw");
    }
}

static void testReplacement(void)
{
    GevrWatchScreenState s = { 0, 0 };

    CHECK(gevrWatchScreenStep(&s, 0, 0) == 0, "nothing while the watch is down");
    CHECK(gevrWatchScreenStep(&s, 0, 1) == 0, "a recentre with the watch down places nothing");
    CHECK(gevrWatchScreenStep(&s, 1, 0) == 1, "placed as the watch comes up");
    for (int f = 0; f < 120; f++) {
        CHECK(gevrWatchScreenStep(&s, 1, 0) == 0, "not again while it stays up (world-locked)");
    }
    CHECK(gevrWatchScreenStep(&s, s.up, 1) == 1, "a recentre while it is up places it again");
    CHECK(gevrWatchScreenStep(&s, 1, 0) == 0, "and it stays there");
    CHECK(gevrWatchScreenStep(&s, 0, 0) == 0, "closing places nothing");
    CHECK(gevrWatchScreenStep(&s, 0, 0) == 0, "still closed");
    CHECK(gevrWatchScreenStep(&s, 1, 0) == 1, "a new pause places it again");
    CHECK(s.placements == 3, "three placements");
}


/* ------------------------------------------------------------------ option C: the terminal */

static void sub3(const float a[3], const float b[3], double o[3])
{
    o[0] = (double)a[0] - b[0];
    o[1] = (double)a[1] - b[1];
    o[2] = (double)a[2] - b[2];
}

static double len3(const double v[3]) { return sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

/* the quad's +Z (its face) in play space */
static void faceNormal(const float q[4], double n[3])
{
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    n[0] = 2.0 * (x * z + w * y);
    n[1] = 2.0 * (y * z - w * x);
    n[2] = 1.0 - 2.0 * (x * x + y * y);
}

static void testTerminalPlacement(void)
{
    static const double yaws[] = { 0.0, 37.0, 90.0, -120.0, 179.0 };
    static const double tilts[][2] = { { 0, 0 }, { -35, 0 }, { 25, 0 }, { 0, 20 }, { -50, -15 } };
    const float standing[3] = { 0.3f, 1.62f, -0.4f };
    const float seated[3] = { -0.1f, 1.12f, 0.2f };

    for (unsigned i = 0; i < sizeof(yaws) / sizeof(yaws[0]); i++) {
        for (int s = 0; s < 2; s++) {
            const float *head = s ? seated : standing;
            GevrTermPanel p = { { 0, 0, 0 }, 0, 0, 0 };
            float q[4], pos[3], quat[4];
            headQuat(yaws[i], 0.0, 0.0, q);
            gevrTermPlace(&p, head, q);
            gevrTermPose(&p, pos, quat);

            const double a = yaws[i] * kPi / 180.0;
            CHECK(NEAR(pos[0], head[0] - sin(a) * 0.9, 1e-4) && NEAR(pos[2], head[2] - cos(a) * 0.9, 1e-4),
                  "terminal: 0.9 m ahead along the yaw");
            CHECK(NEAR(pos[1], head[1] - 0.15, 1e-5), "terminal: centre 15 cm below the eyes (seated too)");
            {
                double n[3], back[3];
                faceNormal(quat, n);
                sub3(head, pos, back);
                CHECK(NEAR((n[0] * back[0] + n[1] * back[1] + n[2] * back[2]) / len3(back), 1.0, 1e-5),
                      "terminal: its face looks straight back at the eyes");
                CHECK(NEAR(len3(n), 1.0, 1e-5), "terminal: unit orientation");
            }
            {
                /* pitched toward the viewer: the bottom edge is nearer the eyes than the top */
                float top[3], bottom[3];
                double dt[3], db[3];
                gevrQuadPoint(pos, quat, GEVR_TERM_WIDTH_M, GEVR_TERM_HEIGHT_M, 0.5f, 0.0f, top);
                gevrQuadPoint(pos, quat, GEVR_TERM_WIDTH_M, GEVR_TERM_HEIGHT_M, 0.5f, 1.0f, bottom);
                sub3(top, head, dt);
                sub3(bottom, head, db);
                CHECK(sqrt(db[0] * db[0] + db[2] * db[2]) < sqrt(dt[0] * dt[0] + dt[2] * dt[2]) - 0.05,
                      "terminal: bottom edge toward the viewer");
                CHECK(NEAR(top[1] - bottom[1], 0.38 * cos(atan2(0.15, 0.9)), 1e-4), "terminal: 38 cm tall, pitched");
            }
            CHECK(NEAR(GEVR_TERM_WIDTH_M, 0.60, 1e-6) && NEAR(GEVR_TERM_HEIGHT_M, 0.38, 1e-6), "terminal: 60 x 38 cm");

            /* the head pitched and rolled: the same panel */
            for (unsigned k = 0; k < sizeof(tilts) / sizeof(tilts[0]); k++) {
                GevrTermPanel t = { { 0, 0, 0 }, 0, 0, 0 };
                float tp[3], tq[4];
                headQuat(yaws[i], tilts[k][0], tilts[k][1], q);
                gevrTermPlace(&t, head, q);
                gevrTermPose(&t, tp, tq);
                CHECK(NEAR(tp[0], pos[0], 1e-3) && NEAR(tp[1], pos[1], 1e-5) && NEAR(tp[2], pos[2], 1e-3),
                      "terminal: head pitch and roll do not move it");
                CHECK(NEAR(fabs(tq[0] * quat[0] + tq[1] * quat[1] + tq[2] * quat[2] + tq[3] * quat[3]), 1.0, 1e-5),
                      "terminal: head pitch and roll do not turn it");
            }
        }
    }

    /* text: a 9-native-pixel line of a ~190-pixel-tall page crop is about 0.9 degrees at 0.91 m */
    {
        const double d = sqrt(0.9 * 0.9 + 0.15 * 0.15);
        const double deg = 2.0 * atan(0.5 * 0.38 * 9.0 / 190.0 / d) * 180.0 / kPi;
        CHECK(deg > 0.8 && deg < 1.3, "terminal: body text about 0.9 degrees");
    }
}

static void testTerminalFollow(void)
{
    const float head[3] = { 0.0f, 1.6f, 0.0f };
    GevrTermPanel p = { { 0, 0, 0 }, 0, 0, 0 };
    float q[4];
    const float dt = 1.0f / 90.0f;

    headQuat(10.0, 0.0, 0.0, q);
    gevrTermPlace(&p, head, q);
    CHECK(p.placements == 1, "follow: placed on open");

    /* within 50 degrees: world-locked, nothing moves */
    for (double y = 10.0; y <= 58.0; y += 2.0) {
        headQuat(y, -20.0, 0.0, q);
        for (int f = 0; f < 10; f++) {
            CHECK(gevrTermFollow(&p, head, q, dt) == 0, "follow: still within 50 degrees");
        }
    }
    CHECK(NEAR(p.yaw, 10.0 * kPi / 180.0, 1e-6), "follow: yaw kept within the dead zone");

    /* past 50 degrees (the head at 75, 65 from the panel): a glide, then it stops */
    {
        const float moved[3] = { 0.4f, 1.3f, -0.2f };   /* stepped aside and sat down */
        double prev = p.yaw, maxStep = 0.0;
        int frames = 0, moving = 1;
        headQuat(75.0, 0.0, 0.0, q);
        while (moving && frames < 900) {
            moving = gevrTermFollow(&p, moved, q, dt);
            const double step = fabs(p.yaw - prev);
            if (step > maxStep) maxStep = step;
            CHECK(p.yaw >= prev - 1e-6, "follow: turns one way only, no overshoot");
            prev = p.yaw;
            frames++;
        }
        CHECK(!moving, "follow: stops");
        CHECK(fabs(p.yaw - 75.0 * kPi / 180.0) < 2.0 * kPi / 180.0, "follow: stops within 2 degrees of the head");
        CHECK(maxStep < 5.0 * kPi / 180.0, "follow: smooth, no frame jumps 5 degrees");
        CHECK(frames > 20 && frames < 180, "follow: a glide of a fraction of a second to two seconds");
        CHECK(NEAR(p.pivot[1], 1.6, 1e-6), "follow: height kept from the open");
        CHECK(fabs(p.pivot[0] - 0.4) < 0.05 && fabs(p.pivot[2] + 0.2) < 0.05, "follow: comes along to the head");
        /* settled: the head moving a little does not start it again */
        headQuat(90.0, 0.0, 0.0, q);
        CHECK(gevrTermFollow(&p, moved, q, dt) == 0, "follow: settled, 15 degrees is within the dead zone");
    }

    /* the other way round, through +-180 */
    {
        GevrTermPanel r = { { 0, 0, 0 }, 0, 0, 0 };
        int frames = 0;
        headQuat(170.0, 0.0, 0.0, q);
        gevrTermPlace(&r, head, q);
        headQuat(-120.0, 0.0, 0.0, q);   /* 70 degrees on, across the seam */
        while (gevrTermFollow(&r, head, q, dt) && frames < 900) frames++;
        CHECK(fabs(r.yaw + 120.0 * kPi / 180.0) < 2.0 * kPi / 180.0, "follow: across +-180 the short way");
    }

    /* a recentre (or a new open) places it at once */
    headQuat(-40.0, 0.0, 0.0, q);
    gevrTermPlace(&p, head, q);
    CHECK(NEAR(p.yaw, -40.0 * kPi / 180.0, 1e-5) && p.following == 0 && p.placements == 2,
          "follow: a recentre places it again, at once");
    CHECK(NEAR(p.pivot[0], 0.0, 1e-6) && NEAR(p.pivot[1], 1.6, 1e-6), "follow: and from the head now");
}

static void testTerminalDock(void)
{
    const float head[3] = { 0.2f, 1.5f, 0.1f };
    GevrTermPanel p = { { 0, 0, 0 }, 0, 0, 0 };
    float q[4], tp[3], tq[4], dp[3], dq[4], size[2];

    headQuat(25.0, 0.0, 0.0, q);
    gevrTermPlace(&p, head, q);
    gevrTermPose(&p, tp, tq);
    gevrTermDockPose(&p, 1280.0f / 960.0f, dp, dq, size);

    CHECK(NEAR(size[1], 0.48, 1e-5) && NEAR(size[0], 0.64, 1e-4), "dock: 64 x 48 cm for a 4:3 page");
    {
        /* to the terminal's right (its +X), at its height */
        float right[3];
        double toDock[3], ax[3];
        gevrQuadPoint(tp, tq, GEVR_TERM_WIDTH_M, GEVR_TERM_HEIGHT_M, 1.0f, 0.5f, right);
        sub3(dp, tp, toDock);
        sub3(right, tp, ax);
        CHECK((toDock[0] * ax[0] + toDock[2] * ax[2]) > 0.0, "dock: on the right");
        CHECK(NEAR(dp[1], tp[1], 1e-5), "dock: same height");
    }
    {
        /* turned 20 degrees in: the angle between the two faces, about the vertical */
        double nt[3], nd[3];
        faceNormal(tq, nt);
        faceNormal(dq, nd);
        const double at = atan2(nt[0], nt[2]), ad = atan2(nd[0], nd[2]);
        double diff = (ad - at) * 180.0 / kPi;
        while (diff > 180.0) diff -= 360.0;
        while (diff < -180.0) diff += 360.0;
        CHECK(NEAR(diff, -20.0, 0.5), "dock: turned 20 degrees in toward the player");
        double back[3];
        sub3(head, dp, back);
        CHECK((nd[0] * back[0] + nd[1] * back[1] + nd[2] * back[2]) / len3(back) > 0.9, "dock: faces the player (within 25 degrees)");
    }
    {
        /* its inner edge sits the gap off the terminal's right edge */
        float edgeT[3], edgeD[3];
        double gap[3];
        gevrQuadPoint(tp, tq, GEVR_TERM_WIDTH_M, GEVR_TERM_HEIGHT_M, 1.0f, 0.5f, edgeT);
        gevrQuadPoint(dp, dq, size[0], size[1], 0.0f, 0.5f, edgeD);
        sub3(edgeD, edgeT, gap);
        CHECK(len3(gap) > 0.02 && len3(gap) < 0.045, "dock: 3 cm off the terminal's edge");
    }
}

static void testQuadHit(void)
{
    const float head[3] = { 0.0f, 1.6f, 0.0f };
    GevrTermPanel p = { { 0, 0, 0 }, 0, 0, 0 };
    float q[4], dp[3], dq[4], size[2];
    headQuat(-65.0, 0.0, 0.0, q);
    gevrTermPlace(&p, head, q);
    gevrTermDockPose(&p, 4.0f / 3.0f, dp, dq, size);

    const float hand[3] = { 0.25f, 1.1f, -0.15f };
    for (int i = 0; i <= 10; i++) {
        for (int j = 0; j <= 10; j++) {
            const float u0 = 0.02f + 0.096f * i, v0 = 0.02f + 0.096f * j;
            float target[3], d[3], u = -1, v = -1;
            gevrQuadPoint(dp, dq, size[0], size[1], u0, v0, target);
            const double dx = target[0] - hand[0], dy = target[1] - hand[1], dz = target[2] - hand[2];
            const double l = sqrt(dx * dx + dy * dy + dz * dz);
            d[0] = (float)(dx / l); d[1] = (float)(dy / l); d[2] = (float)(dz / l);
            CHECK(gevrQuadHit(dp, dq, size[0], size[1], hand, d, &u, &v), "hit: a ray at the dock hits it");
            CHECK(NEAR(u, u0, 1e-4) && NEAR(v, v0, 1e-4), "hit: u, v back (v down)");
            {
                const float away[3] = { -d[0], -d[1], -d[2] };
                CHECK(!gevrQuadHit(dp, dq, size[0], size[1], hand, away, &u, &v), "hit: not behind the hand");
            }
        }
    }
    {
        /* from behind the panel: no hit on its back */
        float c[3], behind[3], d[3], u, v;
        double n[3];
        faceNormal(dq, n);
        gevrQuadPoint(dp, dq, size[0], size[1], 0.5f, 0.5f, c);
        behind[0] = (float)(c[0] - n[0]); behind[1] = (float)(c[1] - n[1]); behind[2] = (float)(c[2] - n[2]);
        d[0] = (float)n[0]; d[1] = (float)n[1]; d[2] = (float)n[2];
        CHECK(!gevrQuadHit(dp, dq, size[0], size[1], behind, d, &u, &v), "hit: not through its back");
        /* past an edge */
        float off[3];
        gevrQuadPoint(dp, dq, size[0], size[1], 1.15f, 0.5f, off);
        const double dx = off[0] - hand[0], dy = off[1] - hand[1], dz = off[2] - hand[2];
        const double l = sqrt(dx * dx + dy * dy + dz * dz);
        d[0] = (float)(dx / l); d[1] = (float)(dy / l); d[2] = (float)(dz / l);
        CHECK(!gevrQuadHit(dp, dq, size[0], size[1], hand, d, &u, &v), "hit: off the edge misses");
    }
}

static void testCrop(void)
{
    const float aspect = 1024.0f / 648.0f;
    float out[4];
    {
        /* the face as drawn (wider than the screen's 4:3 is not needed): kept whole with the margin */
        const float box[4] = { 40.0f, 22.0f, 282.0f, 222.0f };
        CHECK(gevrTermCrop(box, 320.0f, 240.0f, aspect, 0.03f, out) == 1, "crop: a face is a face");
        CHECK(NEAR(out[2] / out[3], aspect, 1e-4), "crop: the texture's aspect");
        const double m = 0.03 * 242.0;
        CHECK(out[0] <= 40.0 - m + 1e-3 && out[0] + out[2] >= 282.0 + m - 1e-3, "crop: the face's width with a margin");
        CHECK(out[1] <= 22.0 - m + 1e-3 && out[1] + out[3] >= 222.0 + m - 1e-3, "crop: the face's height with a margin");
        CHECK(NEAR(out[0] + 0.5 * out[2], 161.0, 1e-3) && NEAR(out[1] + 0.5 * out[3], 122.0, 1e-3), "crop: centred on the face");
    }
    {
        /* a wide face: heightened instead */
        const float box[4] = { 0.0f, 80.0f, 320.0f, 160.0f };
        CHECK(gevrTermCrop(box, 320.0f, 240.0f, aspect, 0.0f, out) == 1, "crop: wide face");
        CHECK(NEAR(out[2], 320.0, 1e-3) && NEAR(out[3], 320.0 / aspect, 1e-3), "crop: wide face heightened");
    }
    {
        /* nothing measured (or a sliver): the whole screen at the texture's aspect, and 0 */
        const float none[4] = { 1e9f, 1e9f, -1e9f, -1e9f };
        const float sliver[4] = { 100.0f, 100.0f, 130.0f, 200.0f };
        CHECK(gevrTermCrop(none, 320.0f, 240.0f, aspect, 0.03f, out) == 0, "crop: nothing measured");
        CHECK(NEAR(out[0] + 0.5 * out[2], 160.0, 1e-3) && out[2] >= 320.0 - 1e-3 && out[3] >= 240.0 - 1e-3,
              "crop: the whole screen instead");
        CHECK(gevrTermCrop(sliver, 320.0f, 240.0f, aspect, 0.03f, out) == 0, "crop: a sliver is not a face");
    }
}

static void testEase(void)
{
    float prev = -1.0f;
    CHECK(NEAR(gevrTermEase(0.0f), 0.0, 1e-7) && NEAR(gevrTermEase(1.0f), 1.0, 1e-7), "ease: 0 and 1");
    CHECK(NEAR(gevrTermEase(-1.0f), 0.0, 1e-7) && NEAR(gevrTermEase(2.0f), 1.0, 1e-7), "ease: clamped");
    for (int i = 0; i <= 100; i++) {
        const float e = gevrTermEase(i / 100.0f);
        CHECK(e >= prev, "ease: monotonic");
        prev = e;
    }
    CHECK(gevrTermEase(0.5f) > 0.8f, "ease: out (fast start, soft landing)");
}

int main(void)
{
    testPlacement();
    testReplacement();
    testTerminalPlacement();
    testTerminalFollow();
    testTerminalDock();
    testQuadHit();
    testCrop();
    testEase();
    if (failures) {
        fprintf(stderr, "test_vr_screen: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_vr_screen: ok\n");
    return 0;
}
