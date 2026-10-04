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

int main(void)
{
    testPlacement();
    testReplacement();
    if (failures) {
        fprintf(stderr, "test_vr_screen: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_vr_screen: ok\n");
    return 0;
}
