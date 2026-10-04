/*
 * Host test of the scope lens's optics (port/vr/vr_scope_math.c, the production file vr_openxr.cpp
 * places the scope's projection layer with). Run: tools/steam_frame/tests/run_tests.sh
 *
 * Wear finding 2026-10-04: "the scope image shifts to the right the closer the rifle comes to the
 * eyes" (held to the right eye). Covers:
 *  - the old lens quad, pushed out along the head's ray as it neared the eyes, sat 10+ degrees right
 *    of the right eye's line of sight at 6 cm (the bug, reproduced from the old placement code);
 *  - now, with the eyepiece on the right eye's axis at 30, 15 and 6 cm, the scope image's centre
 *    (where the gun points, the red sight) is on that eye's line of sight along the gun at every
 *    distance, and the eye sees its whole window filled;
 *  - both eyes look into one image: the same world direction samples the same texel from either eye;
 *  - the other eye at 30 cm sees the image through its own (offset) window; at 6 cm its window falls
 *    past the field and it sees only the dark glass;
 *  - with the gun turned, the image's centre follows the gun's axis, not the lens's position.
 */
#include <math.h>
#include <stdio.h>

#include "vr_scope_math.h"

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", what, __LINE__); failures++; } } while (0)
#define NEAR(a, b, tol) (fabsf((a) - (b)) <= (tol))

static const float kR = 0.0185f;   /* the sniper's eyepiece radius, m (3.7 cm across) */
static const float kEyeL[3] = { -0.032f, 0.0f, 0.0f }, kEyeR[3] = { 0.032f, 0.0f, 0.0f };

/* The old placement (vr_openxr.cpp before this change), for a gun pointing straight ahead with its
 * lens at c: turned toward the head's centre within 8 cm of 12 cm, pushed out along the head's ray
 * to keep its nearest edge 12 cm away. Returns the lens centre as placed. */
static void oldLensPlace(const float c[3], float out[3])
{
    const float MIN = 0.12f, TURN = 0.08f, r = kR;
    float n[3] = { 0.0f, 0.0f, 1.0f };   /* the gun's back */
    const float nearAligned = -c[2] - r * sqrtf(fmaxf(0.0f, 1.0f - n[2] * n[2]));
    float t = (MIN + TURN - nearAligned) / TURN;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float cl = sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
    if (t > 0.0f) {
        for (int i = 0; i < 3; i++) n[i] += (-c[i] / cl - n[i]) * t;
    }
    const float nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    for (int i = 0; i < 3; i++) n[i] /= nl;
    const float nearest = -c[2] - r * sqrtf(fmaxf(0.0f, 1.0f - n[2] * n[2]));
    const float k = nearest < MIN ? MIN / nearest : 1.0f;
    for (int i = 0; i < 3; i++) out[i] = c[i] * k;
}

/* where world direction dir lands in an eye's image of the scope: its tangent in the scope's
 * frame, then that eye's texel (q) and the image texcoord; returns gevrScopeEyeTexcoord's code */
static int sampleDir(const GevrScopeEyeView *v, float field, const float right[3], const float up[3],
                     const float axis[3], const float dir[3], float tex[2])
{
    const float f = dir[0] * axis[0] + dir[1] * axis[1] + dir[2] * axis[2];
    const float t[2] = { (dir[0] * right[0] + dir[1] * right[1] + dir[2] * right[2]) / f,
                         (dir[0] * up[0] + dir[1] * up[1] + dir[2] * up[2]) / f };
    const float q[2] = { (t[0] - v->center[0]) / v->radius, (t[1] - v->center[1]) / v->radius };
    return gevrScopeEyeTexcoord(v, field, q, tex);
}

int main(void)
{
    const float right[3] = { 1.0f, 0.0f, 0.0f }, up[3] = { 0.0f, 1.0f, 0.0f }, axis[3] = { 0.0f, 0.0f, -1.0f };
    const float dists[3] = { 0.30f, 0.15f, 0.06f };

    /* the bug: the old quad at 6 cm in front of the right eye, seen from that eye */
    {
        const float c[3] = { 0.032f, 0.0f, -0.06f };
        float p[3];
        oldLensPlace(c, p);
        const float deg = atanf((p[0] - kEyeR[0]) / -p[2]) * 57.29578f;
        printf("old lens quad at 6 cm: %.1f deg right of the right eye's line of sight\n", deg);
        CHECK(deg > 10.0f, "the old placement put the lens image well right of the aiming eye at 6 cm");
    }

    for (int i = 0; i < 3; i++) {
        const float d = dists[i];
        const float lens[3] = { 0.032f, 0.0f, -d };   /* on the right eye's axis */
        GevrScopeEyeView eyes[2];
        gevrScopeEyeAperture(lens, kR, right, up, axis, kEyeL, &eyes[0]);
        gevrScopeEyeAperture(lens, kR, right, up, axis, kEyeR, &eyes[1]);
        int nearest = -1;
        const float field = gevrScopeField(eyes, &nearest);
        CHECK(nearest == 1, "the right eye is the one looking through it");
        CHECK(eyes[1].visible && NEAR(eyes[1].center[0], 0.0f, 1e-6f) && NEAR(eyes[1].center[1], 0.0f, 1e-6f),
              "the eyepiece is centred on the right eye's line of sight along the gun");
        CHECK(NEAR(eyes[1].radius, kR / d, 1e-5f), "its window is R / d");
        CHECK(field >= eyes[1].radius - 1e-6f, "the field covers the right eye's whole window");

        /* the image's centre (texcoord 0.5, 0.5: the gun's axis, the red sight) is where the right
         * eye looks along the gun, at every distance */
        float tex[2];
        CHECK(sampleDir(&eyes[1], field, right, up, axis, axis, tex) == 1 && NEAR(tex[0], 0.5f, 1e-6f)
              && NEAR(tex[1], 0.5f, 1e-6f), "the image's centre is on the gun's axis for the aiming eye");

        /* both eyes see one image: a direction both windows show samples one texel */
        const float dir[3] = { 0.30f * kR / d * 0.5f, 0.1f * kR / d, -1.0f };
        float texL[2], texR[2];
        const int inR = sampleDir(&eyes[1], field, right, up, axis, dir, texR);
        const int inL = eyes[0].visible ? sampleDir(&eyes[0], field, right, up, axis, dir, texL) : 0;
        if (inR == 1 && inL == 1) {
            CHECK(NEAR(texL[0], texR[0], 1e-5f) && NEAR(texL[1], texR[1], 1e-5f), "one image for both eyes");
        }

        /* the left eye's window */
        int lit = 0, dark = 0;
        for (int y = -20; y <= 20; y++) {
            for (int x = -20; x <= 20; x++) {
                const float q[2] = { x / 20.0f, y / 20.0f };
                const int k = gevrScopeEyeTexcoord(&eyes[0], field, q, tex);
                lit += k == 1;
                dark += k == 2;
            }
        }
        printf("lens %2.0f cm from the right eye: right window r=%.3f, field %.3f; left window at %.3f r=%.3f: "
               "%d%% image, %d%% dark glass\n", d * 100.0f, eyes[1].radius, field, eyes[0].center[0], eyes[0].radius,
               (lit + dark) ? lit * 100 / (lit + dark) : 0, (lit + dark) ? dark * 100 / (lit + dark) : 0);
        if (d >= 0.30f) {
            CHECK(lit > 0 && dark == 0, "at 30 cm the left eye sees the image through its own window");
        }
        if (d <= 0.06f) {
            CHECK(lit == 0 && dark > 0, "at 6 cm the left eye sees only the dark glass");
        }
    }

    /* the gun turned 10 degrees left about up, lens still in front of the right eye: the image's
     * centre follows the axis (10 degrees left), not the lens's place */
    {
        const float a = 10.0f / 57.29578f;
        const float r2[3] = { cosf(a), 0.0f, -sinf(a) }, ax2[3] = { -sinf(a), 0.0f, -cosf(a) };
        const float lens[3] = { 0.032f, 0.0f, -0.10f };
        GevrScopeEyeView eyes[2];
        gevrScopeEyeAperture(lens, kR, r2, up, ax2, kEyeL, &eyes[0]);
        gevrScopeEyeAperture(lens, kR, r2, up, ax2, kEyeR, &eyes[1]);
        const float field = gevrScopeField(eyes, NULL);
        float tex[2];
        const int k = sampleDir(&eyes[1], field, r2, up, ax2, ax2, tex);
        CHECK(k == 1 && NEAR(tex[0], 0.5f, 1e-5f) && NEAR(tex[1], 0.5f, 1e-5f),
              "a turned gun: its axis direction is the image's centre");
    }

    /* an eye in front of the eyepiece (or level with it) does not see it */
    {
        const float lens[3] = { 0.032f, 0.0f, -0.01f };
        GevrScopeEyeView v;
        gevrScopeEyeAperture(lens, kR, right, up, axis, kEyeR, &v);
        CHECK(!v.visible, "an eyepiece 1 cm from the eye is not drawn");
        GevrScopeEyeView none[2] = { v, v };
        int nearest = 0;
        CHECK(gevrScopeField(none, &nearest) == 0.0f && nearest == -1, "no eye sees it: no field");
    }

    if (failures) {
        fprintf(stderr, "test_vr_scope: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_vr_scope: ok\n");
    return 0;
}
