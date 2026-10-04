/*
 * Host test of the 3D cinema's math (port/vr/vr_cinema_math.c, the production functions).
 * Run: tools/steam_frame/tests/run_tests.sh
 *
 * - placement: ahead at the head's yaw, the picture's centre GEVR_CINEMA_DROP_M below the eyes,
 *   the band under it, level whatever the head's pitch or roll;
 * - the per-eye projection views: their fields of view run through the screen's corners, mirror
 *   each other for eyes placed symmetrically, and put every point of the screen at the same place
 *   in both eye images (so the picture lands on one rectangle in the room, with each eye's image);
 * - the shear: the same as two real cameras +-halfSep apart with off-axis windows that meet at the
 *   convergence plane: zero parallax there, the far field parting by 2 halfSep P00 / C;
 * - the far-field limit, the transition fade and the cutscene latch.
 */
#include <math.h>
#include <stdio.h>

#include "vr_cinema_math.h"

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fprintf(stderr, "FAIL (line %d): ", __LINE__); \
    fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); failures++; } } while (0)
static int near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

static void yawQuat(float deg, float q[4])
{
    const float h = deg * 3.14159265f / 360.0f;
    q[0] = 0.0f; q[1] = sinf(h); q[2] = 0.0f; q[3] = cosf(h);
}

static void quatMul(const float a[4], const float b[4], float o[4])
{
    o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

/* a point on the screen at (u, v) of its rectangle (0,0 bottom-left .. 1,1 top-right), play space */
static void screenPoint(const GevrCinemaScreen *s, float u, float v, float out[3])
{
    const float c = cosf(s->yaw), sn = sinf(s->yaw);
    const float lx = (u - 0.5f) * s->width;
    const float ly = (v - 0.5f) * (s->picH + s->bandH);
    out[0] = s->centre[0] + c * lx;
    out[1] = s->centre[1] + ly;
    out[2] = s->centre[2] - sn * lx;
}

/* where a play-space point lands in an eye image (0..1 across, 0..1 up) for a view with the
 * screen's orientation at `eye` and field of view `fov` */
static void imagePos(const GevrCinemaScreen *s, const float eye[3], const float fov[4], const float p[3],
                     float *u, float *v)
{
    const float c = cosf(s->yaw), sn = sinf(s->yaw);
    const float dx = p[0] - eye[0], dy = p[1] - eye[1], dz = p[2] - eye[2];
    const float x = c * dx - sn * dz, y = dy, z = sn * dx + c * dz;   /* view frame: -Z ahead */
    const float tx = x / -z, ty = y / -z;
    *u = (tx - tanf(fov[0])) / (tanf(fov[1]) - tanf(fov[0]));
    *v = (ty - tanf(fov[3])) / (tanf(fov[2]) - tanf(fov[3]));
}

static void testPlacement(void)
{
    const float head[3] = { 0.2f, 1.6f, -0.5f };
    float q[4], pitch[4], tilted[4], roll[4], rolled[4];
    GevrCinemaScreen s, t;

    yawQuat(0.0f, q);
    gevrCinemaPlace(head, q, 4.0f / 3.0f, &s);
    CHECK(near(s.width, GEVR_CINEMA_WIDTH_M, 1e-5f) && near(s.picH, 2.25f, 1e-4f), "4:3 picture 3.0 x 2.25 m (%.3f x %.3f)", s.width, s.picH);
    CHECK(near(s.bandH, 0.45f, 1e-4f), "the band is a fifth of the picture (%.3f)", s.bandH);
    CHECK(near(s.centre[0], 0.2f, 1e-5f) && near(s.centre[2], -3.5f, 1e-5f), "3 m straight ahead (%.3f, %.3f)", s.centre[0], s.centre[2]);
    CHECK(near(s.centre[1] + 0.5f * s.bandH, 1.6f - GEVR_CINEMA_DROP_M, 1e-5f), "the picture's centre 10 cm below the eyes");
    CHECK(near(s.centre[1] + 0.5f * s.bandH - 0.5f * s.picH, s.centre[1] - 0.5f * (s.picH + s.bandH) + s.bandH, 1e-5f),
          "the band is under the picture");

    /* a head pitched down 30 degrees and rolled 20: the same screen */
    {
        const float h = -30.0f * 3.14159265f / 360.0f, r = 20.0f * 3.14159265f / 360.0f;
        pitch[0] = sinf(h); pitch[1] = 0.0f; pitch[2] = 0.0f; pitch[3] = cosf(h);
        roll[0] = 0.0f; roll[1] = 0.0f; roll[2] = sinf(r); roll[3] = cosf(r);
        quatMul(q, pitch, tilted);
        quatMul(tilted, roll, rolled);
    }
    gevrCinemaPlace(head, rolled, 4.0f / 3.0f, &t);
    CHECK(near(t.yaw, s.yaw, 1e-4f) && near(t.centre[0], s.centre[0], 1e-4f) && near(t.centre[1], s.centre[1], 1e-4f)
          && near(t.centre[2], s.centre[2], 1e-4f), "pitch and roll do not move or tilt it");

    /* yaw 90 (looking along -X) */
    yawQuat(90.0f, q);
    gevrCinemaPlace(head, q, 4.0f / 3.0f, &t);
    CHECK(near(t.centre[0], 0.2f - 3.0f, 1e-4f) && near(t.centre[2], -0.5f, 1e-4f), "yaw 90: 3 m along -X (%.3f, %.3f)", t.centre[0], t.centre[2]);

    /* a new aspect keeps the picture's centre */
    {
        const float before = s.centre[1] + 0.5f * s.bandH;
        gevrCinemaSetAspect(&s, 16.0f / 9.0f);
        CHECK(near(s.centre[1] + 0.5f * s.bandH, before, 1e-5f) && near(s.picH, 3.0f * 9.0f / 16.0f, 1e-4f),
              "16:9: the picture's centre stays, 1.69 m tall");
    }
}

static void testEyeViews(float yawDeg, const float head[3])
{
    float q[4], fov[2][4], eye[2][3], depth;
    GevrCinemaScreen s;
    const float c = cosf(yawDeg * 3.14159265f / 180.0f), sn = sinf(yawDeg * 3.14159265f / 180.0f);
    int e, i, ok = 1;

    yawQuat(yawDeg, q);
    gevrCinemaPlace(head, q, 4.0f / 3.0f, &s);
    for (e = 0; e < 2; e++) {
        const float x = e == 0 ? -0.032f : 0.032f;   /* along the head's right: (cos yaw, 0, -sin yaw) */
        eye[e][0] = head[0] + c * x;
        eye[e][1] = head[1];
        eye[e][2] = head[2] - sn * x;
        CHECK(gevrCinemaEyeFov(&s, eye[e], fov[e], &depth), "yaw %.0f: eye %d has a view", yawDeg, e);
        CHECK(near(depth, 3.0f, 1e-4f), "yaw %.0f: eye %d is 3 m in front (%.4f)", yawDeg, e, depth);
        CHECK(fov[e][0] < 0.0f && fov[e][1] > 0.0f && fov[e][2] > 0.0f && fov[e][3] < 0.0f, "yaw %.0f: signs", yawDeg);
    }
    /* symmetric eyes: mirrored fields of view, equal vertically */
    CHECK(near(fov[0][0], -fov[1][1], 1e-5f) && near(fov[0][1], -fov[1][0], 1e-5f), "yaw %.0f: L/R mirror", yawDeg);
    CHECK(near(fov[0][2], fov[1][2], 1e-6f) && near(fov[0][3], fov[1][3], 1e-6f), "yaw %.0f: same up/down", yawDeg);
    /* each eye looks a little across: the left eye's view leans right of centre */
    CHECK(fov[0][1] > -fov[0][0], "yaw %.0f: left eye's window is off-axis to the right", yawDeg);
    /* the corners and every screen point land at the same image position in both eyes */
    for (i = 0; i <= 4; i++) {
        const float u = i / 4.0f, v = (4 - i) / 4.0f * 0.9f + 0.05f;
        float p[3], u0, v0, u1, v1;
        screenPoint(&s, u, v, p);
        imagePos(&s, eye[0], fov[0], p, &u0, &v0);
        imagePos(&s, eye[1], fov[1], p, &u1, &v1);
        ok = ok && near(u0, u, 1e-4f) && near(v0, v, 1e-4f) && near(u1, u, 1e-4f) && near(v1, v, 1e-4f);
    }
    CHECK(ok, "yaw %.0f: screen points land at their own image position in both eyes (the image converges on the screen)", yawDeg);
}

static void testMovedHead(void)
{
    /* placed, then the head steps right and down: the views still run through the same corners */
    const float head[3] = { 0.0f, 1.6f, 0.0f };
    float q[4], eye[3] = { 0.4f, 1.2f, 0.5f }, fov[4], p[3], u, v;
    GevrCinemaScreen s;

    yawQuat(30.0f, q);
    gevrCinemaPlace(head, q, 4.0f / 3.0f, &s);
    CHECK(gevrCinemaEyeFov(&s, eye, fov, NULL), "moved head: a view");
    screenPoint(&s, 0.0f, 1.0f, p);
    imagePos(&s, eye, fov, p, &u, &v);
    CHECK(near(u, 0.0f, 1e-4f) && near(v, 1.0f, 1e-4f), "moved head: the top-left corner is the image's (%.4f, %.4f)", u, v);
    screenPoint(&s, 1.0f, 0.0f, p);
    imagePos(&s, eye, fov, p, &u, &v);
    CHECK(near(u, 1.0f, 1e-4f) && near(v, 0.0f, 1e-4f), "moved head: the bottom-right corner is the image's (%.4f, %.4f)", u, v);
    /* behind the screen: no view */
    {
        float behind[3];
        screenPoint(&s, 0.5f, 0.5f, behind);
        behind[0] -= sinf(s.yaw) * 0.5f;   /* 0.5 m behind the face (the face looks along +Z of its yaw) */
        behind[2] -= cosf(s.yaw) * 0.5f;
        CHECK(!gevrCinemaEyeFov(&s, behind, fov, NULL), "an eye behind the screen gets no view");
    }
}

/* NDC x of a view-space point for a camera at x = cam with an off-axis window: the mono camera's
 * window at depth C ([-C/P00, C/P00]), seen from the moved camera */
static float offAxisNdc(float p00, float cam, float C, float xv, float depth)
{
    const float l = -C / p00 - cam, r = C / p00 - cam;   /* the window in the camera's own coordinates */
    const float onPlane = (xv - cam) * C / depth;
    return (2.0f * onPlane - (l + r)) / (r - l);
}

static void testShear(void)
{
    const float p00 = 1.299f;                 /* GoldenEye: fovy 60, 4:3 */
    const float scale = 100.0f * 0.2f;        /* view units per metre on the Dam */
    const float h = 0.032f * scale, C = 3.0f * scale;
    float o[8];
    int ok = 1, i;

    gevrCinemaShear(p00, h, C, o);
    CHECK(near(o[0], -o[4], 1e-6f) && near(o[1], -o[5], 1e-6f) && o[2] == 0.0f && o[3] == 0.0f && o[6] == 0.0f && o[7] == 0.0f,
          "the eyes' offsets are symmetric, nothing on the HUD terms");
    CHECK(o[0] < 0.0f, "left eye: the camera moves left, so the world moves right (ex < 0)");
    for (i = 0; i < 40; i++) {
        const float xv = -30.0f + 1.7f * i, depth = 2.0f + 9.0f * i;
        int e;
        for (e = 0; e < 2; e++) {
            const float *eo = o + 4 * e;
            const float shader = (p00 * xv - eo[0] - eo[1] * depth) / depth;
            const float camera = offAxisNdc(p00, e == 0 ? -h : h, C, xv, depth);
            ok = ok && near(shader, camera, 1e-4f);
        }
    }
    CHECK(ok, "the shader's shear is two real cameras +-halfSep apart with windows meeting at C");
    {
        const float xv = 7.0f;
        const float l = (p00 * xv - o[0] - o[1] * C) / C, r = (p00 * xv - o[4] - o[5] * C) / C;
        CHECK(near(l, r, 1e-6f), "zero parallax at the convergence plane");
    }
    {
        const float far = 1e7f, xv = 0.0f;
        const float l = (p00 * xv - o[0] - o[1] * far) / far, r = (p00 * xv - o[4] - o[5] * far) / far;
        CHECK(near(r - l, 2.0f * h * p00 / C, 1e-5f) && r > l, "far field: uncrossed by 2 h P00 / C (%.5f)", r - l);
    }
    /* the far field on the 3 m screen: 3.0 * 1.299 * 0.032 / 3 = 41.6 mm, under 0.75 x 64 mm */
    {
        const float fp = gevrCinemaFarParallax(p00, 0.032f, 3.0f, 3.0f);
        CHECK(near(fp, 0.04157f, 2e-4f), "far parallax 41.6 mm (%.4f)", fp);
        CHECK(near(gevrCinemaSafeHalfSep(p00, 0.032f, 3.0f, 3.0f, 0.064f), 0.032f, 1e-6f), "64 mm kept at IPD 64");
        /* a zoomed camera (fovy 20: P00 = 4.25) would part the far field by 136 mm: held to 48 */
        {
            const float zoom = 1.0f / tanf(10.0f * 3.14159265f / 180.0f) * 0.75f;
            const float hs = gevrCinemaSafeHalfSep(zoom, 0.032f, 3.0f, 3.0f, 0.064f);
            CHECK(hs < 0.032f && near(gevrCinemaFarParallax(zoom, hs, 3.0f, 3.0f), 0.75f * 0.064f, 1e-4f),
                  "zoomed: the separation shrinks so the far field stays at 0.75 IPD (%.4f)", hs);
        }
    }
}

static void testFadeAndLatch(void)
{
    GevrCinemaLatch l = { 0, 0, 0 };
    int i, on;

    CHECK(gevrCinemaFadeAlpha(-0.1f) == 0.0f && gevrCinemaFadeAlpha(0.0f) == 1.0f && gevrCinemaFadeAlpha(0.24f) == 1.0f,
          "fade: black for the first 0.25 s");
    CHECK(near(gevrCinemaFadeAlpha(0.25f + 0.175f), 0.5f, 1e-4f) && gevrCinemaFadeAlpha(0.61f) == 0.0f,
          "fade: half at 0.425 s, clear from 0.6 s");

    CHECK(gevrCinemaLatchStep(&l, 0, 0) == 0, "latch: off with no cutscene");
    CHECK(gevrCinemaLatchStep(&l, 1, 0) == 1 && l.starts == 1, "latch: on at the first cutscene frame");
    /* one-frame hand-overs through CAMERAMODE_NONE do not drop it */
    on = 1;
    for (i = 0; i < 30; i++) on = on && gevrCinemaLatchStep(&l, (i % 3) != 1, 0);
    CHECK(on && l.starts == 1, "latch: holds through flickering frames");
    for (i = 0; i < GEVR_CINEMA_RELEASE_FRAMES - 1; i++) gevrCinemaLatchStep(&l, 0, 0);
    CHECK(l.on == 1, "latch: still on %d frames after the last cutscene frame", GEVR_CINEMA_RELEASE_FRAMES - 1);
    CHECK(gevrCinemaLatchStep(&l, 0, 0) == 0, "latch: off after %d quiet frames", GEVR_CINEMA_RELEASE_FRAMES);
    gevrCinemaLatchStep(&l, 1, 0);
    CHECK(gevrCinemaLatchStep(&l, 0, 1) == 0 && l.starts == 2, "latch: stereo play takes over at once");
}

int main(void)
{
    const float head0[3] = { 0.0f, 1.6f, 0.0f };
    const float head1[3] = { -1.2f, 1.1f, 2.3f };

    testPlacement();
    testEyeViews(0.0f, head0);
    testEyeViews(90.0f, head1);
    testEyeViews(-135.0f, head1);
    testMovedHead();
    testShear();
    testFadeAndLatch();
    if (failures) {
        fprintf(stderr, "%d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("vr_cinema: %d checks passed\n", checks);
    return 0;
}
