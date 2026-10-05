/*
 * Host test of the scope lens (port/vr/vr_scope_math.c, the production functions vr_openxr.cpp and
 * bondview2.c use). Run: tools/steam_frame/tests/run_tests.sh
 *
 * Round-3 wear (2026-10-05, 3c7aa52): "the zoomed viewport slides off to the left, out of the
 * scope, as you aim, and it doesn't match the true aim". Checked against an independent reference
 * (a plain pinhole eye and a plain look-at camera written here, not the production code's):
 *  - the old optics mapping (4811c1e), fed the pose from the wear's journal, puts the aim point
 *    (the collimated image's centre) outside the eyepiece as the right eye saw it: the bug;
 *  - the scope camera: the shot ray's far point lands on the picture's centre, the gun's right and
 *    up on the picture's right and up, for any roll;
 *  - the disc, for many gun poses about the eye (yaw, pitch, roll, eye relief 6-40 cm, off the
 *    eye's line): every point of the eyepiece's circle, projected into either eye by the pinhole,
 *    lies in that eye's view box and maps onto the picture's edge at its own angle; the eyepiece's
 *    centre maps onto the picture's centre (the aim point) from both eyes; just outside the circle
 *    is clear;
 *  - each eye's view, turned to look straight at the eyepiece, has a box round its own axis;
 *  - the aiming eye is the one behind the eyepiece; the disc's tangent is its radius over distance.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "vr_scope_math.h"

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", what, __LINE__); failures++; } } while (0)

static const float kR = 0.0185f;   /* the sniper's eyepiece radius, m (3.7 cm across) */

static void cross(const float a[3], const float b[3], float o[3])
{
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
static float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/* rotation matrix (columns = the turned x, y, z) from yaw (about y), pitch (about x), roll (about z), degrees */
static void ypr(float yaw, float pitch, float roll, float m[3][3])
{
    const float k = 3.14159265f / 180.0f;
    const float cy = cosf(yaw * k), sy = sinf(yaw * k), cp = cosf(pitch * k), sp = sinf(pitch * k);
    const float cr = cosf(roll * k), sr = sinf(roll * k);
    /* R = Ry Rx Rz */
    const float Ry[3][3] = { { cy, 0, sy }, { 0, 1, 0 }, { -sy, 0, cy } };
    const float Rx[3][3] = { { 1, 0, 0 }, { 0, cp, -sp }, { 0, sp, cp } };
    const float Rz[3][3] = { { cr, -sr, 0 }, { sr, cr, 0 }, { 0, 0, 1 } };
    float t[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            t[i][j] = Ry[i][0] * Rx[0][j] + Ry[i][1] * Rx[1][j] + Ry[i][2] * Rx[2][j];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            m[i][j] = t[i][0] * Rz[0][j] + t[i][1] * Rz[1][j] + t[i][2] * Rz[2][j];
}

/* the reference pinhole: a point in eye space to its tangents */
static void pinhole(const float p[3], float t[2])
{
    t[0] = p[0] / -p[2];
    t[1] = p[1] / -p[2];
}

/* the column-major GL matrix times a point */
static void project(const float vp[16], const float p[3], float ndc[3])
{
    float c[4];
    for (int r = 0; r < 4; r++) {
        c[r] = vp[0 * 4 + r] * p[0] + vp[1 * 4 + r] * p[1] + vp[2 * 4 + r] * p[2] + vp[3 * 4 + r];
    }
    ndc[0] = c[0] / c[3];
    ndc[1] = c[1] / c[3];
    ndc[2] = c[2] / c[3];
}

static float frand(float lo, float hi) { return lo + (hi - lo) * (float)rand() / (float)RAND_MAX; }

/* 1. The old mapping (4811c1e), with the pose from the wear's journal (10:16:21, right eye). */
static void test_old_optics_missed_the_aim(void)
{
    /* "scope: hand 0 lens at (0.001 0.011 -0.151) m, axis (-0.02 0.10 -1.00) ... the right eye looks
     * through it (window at -0.206,0.017 r 0.122 ...)" */
    float axis[3] = { -0.02f, 0.10f, -1.00f };
    const float al = sqrtf(dot(axis, axis));
    for (int i = 0; i < 3; i++) axis[i] /= al;
    const float lens[3] = { 0.001f, 0.011f, -0.151f }, eye[3] = { 0.032f, 0.0f, 0.0f };
    float right[3], up[3];
    const float wup[3] = { 0, 1, 0 };
    cross(axis, wup, right);
    const float rl = sqrtf(dot(right, right));
    for (int i = 0; i < 3; i++) right[i] /= rl;
    cross(right, axis, up);
    const float d[3] = { lens[0] - eye[0], lens[1] - eye[1], lens[2] - eye[2] };
    const float depth = dot(d, axis);
    const float c[2] = { dot(d, right) / depth, dot(d, up) / depth };
    const float r = kR / depth;
    /* the collimated image's centre (the aim point, tangent 0) as seen through that window */
    const float off = sqrtf(c[0] * c[0] + c[1] * c[1]);
    printf("old optics, wear pose: window centre %.3f,%.3f r %.3f: the aim point is %.2f radii from the "
           "eyepiece's centre (outside: > 1)\n", c[0], c[1], r, off / r);
    CHECK(off / r > 1.0f, "the old mapping reproduces the wear: the aim point outside the eyepiece");
}

/* 2. The scope camera: the shot's far point at the picture's centre, the gun's right and up at its
 * right and up edges, at any roll. */
static void test_camera(void)
{
    srand(7);
    int n = 0;
    for (int i = 0; i < 2000; i++) {
        float m[3][3];
        ypr(frand(-180, 180), frand(-80, 80), frand(-180, 180), m);
        const float dir[3] = { -m[0][2], -m[1][2], -m[2][2] };   /* the gun's -back */
        const float gup[3] = { m[0][1], m[1][1], m[2][1] };
        const float gright[3] = { m[0][0], m[1][0], m[2][0] };
        const float o[3] = { frand(-5, 5), frand(-5, 5), frand(-5, 5) };
        const float fov = frand(1.0f, 60.0f), nearZ = 1.0f, farZ = 5000.0f;
        float vp[16];
        if (!gevrScopeCameraMatrix(o, dir, gup, fov, nearZ, farZ, vp)) {
            CHECK(0, "camera matrix");
            continue;
        }
        const float D = 1000.0f, t = tanf(fov * 0.5f * 3.14159265f / 180.0f);
        const float far[3] = { o[0] + dir[0] * D, o[1] + dir[1] * D, o[2] + dir[2] * D };
        float ndc[3];
        project(vp, far, ndc);
        if (!(fabsf(ndc[0]) < 1e-3f && fabsf(ndc[1]) < 1e-3f && ndc[2] > -1.0f && ndc[2] < 1.0f)) {
            CHECK(0, "the shot ray's far point is the picture's centre");
            continue;
        }
        const float pr[3] = { far[0] + gright[0] * D * t, far[1] + gright[1] * D * t, far[2] + gright[2] * D * t };
        project(vp, pr, ndc);
        CHECK(fabsf(ndc[0] - 1.0f) < 2e-3f && fabsf(ndc[1]) < 2e-3f, "the gun's right is the picture's right edge");
        const float pu[3] = { far[0] + gup[0] * D * t, far[1] + gup[1] * D * t, far[2] + gup[2] * D * t };
        project(vp, pu, ndc);
        CHECK(fabsf(ndc[1] - 1.0f) < 2e-3f && fabsf(ndc[0]) < 2e-3f, "the gun's up is the picture's top edge");
        n++;
    }
    printf("camera: %d random gun poses, the shot ray's far point at the picture's centre, upright with the gun\n", n);
}

/* 3. The disc from both eyes, many held poses. */
static void test_disc(void)
{
    srand(11);
    const float eyes[2][3] = { { -0.032f, 0.0f, 0.0f }, { 0.032f, 0.0f, 0.0f } };
    int poses = 0, seen = 0, aimRight = 0;
    float worstEdge = 0.0f, worstCentre = 0.0f;
    for (int i = 0; i < 4000; i++) {
        /* the eyepiece d ahead of the right eye, off its line by up to 2 cm; the gun turned about it */
        const float d = frand(0.06f, 0.40f);
        const float lensH[3] = { 0.032f + frand(-0.02f, 0.02f), frand(-0.02f, 0.02f), -d };
        float m[3][3];
        ypr(frand(-40, 40), frand(-30, 30), frand(-90, 90), m);
        const float right[3] = { m[0][0], m[1][0], m[2][0] }, up[3] = { m[0][1], m[1][1], m[2][1] };
        const float axis[3] = { -m[0][2], -m[1][2], -m[2][2] };
        GevrScopeDisc disc[2];
        for (int e = 0; e < 2; e++) {
            const float le[3] = { lensH[0] - eyes[e][0], lensH[1] - eyes[e][1], lensH[2] - eyes[e][2] };
            /* the view turned to look at the eyepiece (vr_openxr.cpp does the same) */
            float bx[3], by[3], bz[3];
            gevrScopeViewBasis(le, bx, by, bz);
            CHECK(fabsf(dot(bx, by)) < 1e-5f && fabsf(dot(bx, bz)) < 1e-5f && fabsf(dot(by, bz)) < 1e-5f
                  && fabsf(bx[1]) < 1e-5f && by[1] > 0.0f, "the view basis is orthonormal, level, upright");
            const float lv[3] = { dot(le, bx), dot(le, by), dot(le, bz) };
            const float rv[3] = { dot(right, bx), dot(right, by), dot(right, bz) };
            const float uv[3] = { dot(up, bx), dot(up, by), dot(up, bz) };
            const float av[3] = { dot(axis, bx), dot(axis, by), dot(axis, bz) };
            gevrScopeDisc(lv, kR, rv, uv, av, &disc[e]);
            if (disc[e].visible) {
                CHECK(disc[e].box[0] < 0.0f && disc[e].box[1] > 0.0f && disc[e].box[3] < 0.0f && disc[e].box[2] > 0.0f,
                      "the turned view's box straddles its axis");
            }
        }
        poses++;
        const int aim = gevrScopeAimEye(disc, NULL);
        if (aim == 1) aimRight++;
        for (int e = 0; e < 2; e++) {
            if (!disc[e].visible) continue;
            seen++;
            const GevrScopeDisc *v = &disc[e];
            /* the centre: the aim point */
            {
                float t[2], tex[2], rim;
                pinhole(v->lens, t);
                const int in = gevrScopeDiscTexcoord(v, t, tex, &rim);
                const float err = fabsf(tex[0] - 0.5f) + fabsf(tex[1] - 0.5f);
                if (err > worstCentre) worstCentre = err;
                CHECK(in == 1 && err < 1e-3f, "the eyepiece's centre shows the aim point, from either eye");
            }
            for (int k = 0; k < 24; k++) {
                const float a = (float)k * 6.2831853f / 24.0f;
                for (int s = 0; s < 2; s++) {
                    const float rr = kR * (s == 0 ? 0.995f : 1.01f);
                    const float p[3] = { v->lens[0] + (v->right[0] * cosf(a) + v->up[0] * sinf(a)) * rr,
                                         v->lens[1] + (v->right[1] * cosf(a) + v->up[1] * sinf(a)) * rr,
                                         v->lens[2] + (v->right[2] * cosf(a) + v->up[2] * sinf(a)) * rr };
                    float t[2], tex[2], rim;
                    pinhole(p, t);
                    const int in = gevrScopeDiscTexcoord(v, t, tex, &rim);
                    if (s == 0) {
                        CHECK(t[0] >= v->box[0] && t[0] <= v->box[1] && t[1] >= v->box[3] && t[1] <= v->box[2],
                              "the eyepiece's circle inside the eye's view box");
                        const float ex = 0.5f + 0.5f * 0.995f * cosf(a), ey = 0.5f + 0.5f * 0.995f * sinf(a);
                        const float err = fabsf(tex[0] - ex) + fabsf(tex[1] - ey);
                        if (err > worstEdge) worstEdge = err;
                        CHECK(in == 1 && err < 2e-3f, "the circle's edge is the picture's edge at its own angle");
                    } else {
                        CHECK(in == 0, "just outside the eyepiece: clear");
                    }
                }
            }
        }
    }
    printf("disc: %d held poses (yaw +-40, pitch +-30, roll +-90, 6-40 cm), %d eye views; worst centre %.5f, "
           "edge %.5f (texcoord); the right eye aiming in %d\n", poses, seen, worstCentre, worstEdge, aimRight);
    CHECK(aimRight > poses * 9 / 10, "the eye behind the eyepiece is the aiming eye");
}

/* 4. Aiming eye, magnification and the eye in front of the eyepiece. */
static void test_aim_eye(void)
{
    const float right[3] = { 1, 0, 0 }, up[3] = { 0, 1, 0 }, axis[3] = { 0, 0, -1 };
    GevrScopeDisc disc[2];
    /* the eyepiece 15 cm ahead of the left eye */
    const float l0[3] = { 0.0f, 0.0f, -0.15f }, l1[3] = { -0.064f, 0.0f, -0.15f };
    gevrScopeDisc(l0, kR, right, up, axis, &disc[0]);
    gevrScopeDisc(l1, kR, right, up, axis, &disc[1]);
    CHECK(gevrScopeAimEye(disc, NULL) == 0, "the left eye behind the eyepiece aims");
    CHECK(fabsf(gevrScopeDiscTan(&disc[0]) - kR / 0.15f) < 1e-5f, "the disc's tangent from the aiming eye");
    /* in front of the eyepiece (the gun pointing back at the eye): not seen */
    const float back[3] = { 0, 0, 1 };
    GevrScopeDisc b;
    gevrScopeDisc(l0, kR, right, up, back, &b);
    CHECK(!b.visible, "an eyepiece facing away is not seen");
    /* reaching the eye's plane: not seen */
    const float nearL[3] = { 0.0f, 0.0f, -0.010f };
    float tilt[3] = { 0.0f, -0.8f, -0.6f };
    float tr[3], tu[3];
    cross(tilt, (float[3]){ 0, 1, 0 }, tr);
    const float trl = sqrtf(dot(tr, tr));
    for (int i = 0; i < 3; i++) tr[i] /= trl;
    cross(tr, tilt, tu);
    gevrScopeDisc(nearL, kR, tr, tu, tilt, &b);
    CHECK(!b.visible, "a disc crossing the eye's plane is not drawn");
}

int main(void)
{
    test_old_optics_missed_the_aim();
    test_camera();
    test_disc();
    test_aim_eye();
    if (failures) {
        fprintf(stderr, "test_vr_scope: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_vr_scope: ok\n");
    return 0;
}
