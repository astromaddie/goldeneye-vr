/*
 * Host test of the gun's aim math (port/vr/vr_aim_math.c, the production file that bondview2.c
 * gevrHandAxes, vr_input.cpp and the scope lens use). Run: tools/steam_frame/tests/run_tests.sh
 *
 * Covers wear finding 2026-10-04 (the gun aimed 30-45 degrees below where the hand points):
 *  - the grip map is the one measured on Quest (right +X, up -Z, back +Y; barrel -Y);
 *  - with the offset measured from an aim pose turned 40 degrees about the grip's X (either way),
 *    the gun's barrel is the aim pose's -Z, its up the aim's +Y, its right the aim's +X;
 *  - with an aim pose pitched up from a level barrel the gun's barrel points up (the sign);
 *  - the offset is rigid: measured at one controller orientation, it fits any other;
 *  - with the identity offset the gun's axes are exactly the old grip axes.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "vr_aim_math.h"

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", what, __LINE__); failures++; } } while (0)

static void axisAngle(float ax, float ay, float az, float deg, float q[4])
{
    const float h = deg * 3.14159265f / 360.0f, n = sqrtf(ax * ax + ay * ay + az * az);
    q[0] = ax / n * sinf(h);
    q[1] = ay / n * sinf(h);
    q[2] = az / n * sinf(h);
    q[3] = cosf(h);
}

/* v' = q v q^-1 */
static void rotate(const float q[4], const float v[3], float out[3])
{
    const float p[4] = { v[0], v[1], v[2], 0.0f }, c[4] = { -q[0], -q[1], -q[2], q[3] };
    float t[4], r[4];
    gevrQuatMul(q, p, t);
    gevrQuatMul(t, c, r);
    out[0] = r[0];
    out[1] = r[1];
    out[2] = r[2];
}

static int near3(const float a[3], const float b[3], float eps)
{
    return fabsf(a[0] - b[0]) < eps && fabsf(a[1] - b[1]) < eps && fabsf(a[2] - b[2]) < eps;
}

/* the gun's barrel/up/right for a grip and an offset, as the game takes them */
static void gunAxes(const float grip[4], const float off[4], float barrel[3], float up[3], float right[3])
{
    float g[4], back[3];
    gevrGunQuat(grip, off, g);
    gevrGripAxesFromQuat(g, right, up, back);
    barrel[0] = -back[0];
    barrel[1] = -back[1];
    barrel[2] = -back[2];
}

static void checkAimFollowed(const float grip[4], const float aim[4], const char *what)
{
    static const float negZ[3] = { 0, 0, -1 }, posY[3] = { 0, 1, 0 }, posX[3] = { 1, 0, 0 };
    float off[4], barrel[3], up[3], right[3], want[3];
    char msg[160];

    gevrAimOffsetFromPoses(grip, aim, off);
    gunAxes(grip, off, barrel, up, right);
    rotate(aim, negZ, want);
    snprintf(msg, sizeof(msg), "%s: the barrel is the aim pose's -Z", what);
    CHECK(near3(barrel, want, 1e-4f), msg);
    rotate(aim, posY, want);
    snprintf(msg, sizeof(msg), "%s: the gun's up is the aim pose's +Y", what);
    CHECK(near3(up, want, 1e-4f), msg);
    rotate(aim, posX, want);
    snprintf(msg, sizeof(msg), "%s: the gun's right is the aim pose's +X", what);
    CHECK(near3(right, want, 1e-4f), msg);
}

int main(void)
{
    static const float identity[4] = { 0, 0, 0, 1 };
    float q[4], t[4], grip[4], aim[4], rot40[4], off[4];
    float right[3], up[3], back[3];

    /* the grip map at the identity: right +X, up -Z, back +Y (barrel -Y) */
    gevrGripAxesFromQuat(identity, right, up, back);
    {
        static const float r[3] = { 1, 0, 0 }, u[3] = { 0, 0, -1 }, b[3] = { 0, 1, 0 };
        CHECK(near3(right, r, 1e-6f) && near3(up, u, 1e-6f) && near3(back, b, 1e-6f), "the grip map (Quest-measured)");
    }

    /* a synthetic grip: yawed 30, pitched 10, rolled -20 degrees */
    axisAngle(0, 1, 0, 30.0f, q);
    axisAngle(1, 0, 0, 10.0f, t);
    gevrQuatMul(q, t, grip);
    axisAngle(0, 0, 1, -20.0f, t);
    memcpy(q, grip, sizeof(q));
    gevrQuatMul(q, t, grip);

    /* an aim pose turned 40 degrees about the grip's own X, either way */
    axisAngle(1, 0, 0, 40.0f, rot40);
    gevrQuatMul(grip, rot40, aim);
    checkAimFollowed(grip, aim, "aim = grip * Rx(+40)");
    gevrAimOffsetFromPoses(grip, aim, off);
    CHECK(fabsf(gevrQuatAngleDeg(off) - 130.0f) < 0.05f, "aim = grip * Rx(+40): the offset is a 130 degree turn (Rx(40) Rx(90))");
    axisAngle(1, 0, 0, -40.0f, rot40);
    gevrQuatMul(grip, rot40, aim);
    checkAimFollowed(grip, aim, "aim = grip * Rx(-40)");

    /* an arbitrary aim (a Frame-like offset with a little yaw and roll as well) */
    axisAngle(0.9f, 0.2f, -0.1f, 47.0f, rot40);
    gevrQuatMul(grip, rot40, aim);
    checkAimFollowed(grip, aim, "an arbitrary grip-to-aim turn");

    /* rigid: measured once, the offset carries the gun to the aim at any other orientation */
    {
        float grip2[4], aim2[4], turn[4], barrel[3], upv[3], rightv[3], want[3];
        static const float negZ[3] = { 0, 0, -1 };
        gevrAimOffsetFromPoses(grip, aim, off);
        axisAngle(0.3f, -1.0f, 0.4f, 75.0f, turn);
        gevrQuatMul(turn, grip, grip2);
        gevrQuatMul(turn, aim, aim2);
        gunAxes(grip2, off, barrel, upv, rightv);
        rotate(aim2, negZ, want);
        CHECK(near3(barrel, want, 1e-4f), "the offset is rigid: it holds after the controller turns");
    }

    /* the sign: a level barrel (the grip pitched +90 about X, as gevrVrDiagParkHands) and an
     * aim pose pitched 40 degrees up: the gun's barrel points up, 40 degrees */
    {
        float barrel[3], upv[3], rightv[3];
        axisAngle(1, 0, 0, 90.0f, grip);
        axisAngle(1, 0, 0, 40.0f, aim);
        gevrGripAxesFromQuat(grip, right, up, back);
        CHECK(fabsf(-back[2] + 1.0f) < 1e-5f, "the parked grip's barrel is forward (-Z)");
        gevrAimOffsetFromPoses(grip, aim, off);
        gunAxes(grip, off, barrel, upv, rightv);
        CHECK(fabsf(barrel[1] - sinf(40.0f * 3.14159265f / 180.0f)) < 1e-4f && barrel[2] < 0.0f,
              "an aim pitched up 40 degrees lifts the barrel 40 degrees, still forward");
    }

    /* identity offset: the gun's axes are the grip's, exactly */
    {
        float g[4], r2[3], u2[3], b2[3];
        gevrGripAxesFromQuat(grip, right, up, back);
        gevrGunQuat(grip, identity, g);
        gevrGripAxesFromQuat(g, r2, u2, b2);
        CHECK(near3(right, r2, 1e-6f) && near3(up, u2, 1e-6f) && near3(back, b2, 1e-6f),
              "with the identity offset the gun's axes are the old grip axes");
        gevrAimOffsetFromPoses(grip, grip, off);
        CHECK(fabsf(gevrQuatAngleDeg(off) - 90.0f) < 0.05f, "aim = grip gives the Rx(90) map, not the identity");
    }

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("vr_aim_math: all checks passed\n");
    return 0;
}
