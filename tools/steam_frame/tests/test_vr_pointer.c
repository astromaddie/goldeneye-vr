/*
 * Host test of the menu pointer's quads (port/vr/vr_pointer_math.c, the production functions
 * vr_openxr.cpp submits the pointer with). Run: tools/steam_frame/tests/run_tests.sh
 *
 * Round-3 wear (2026-10-05): the laser showed behind the floating screen although its layer was
 * submitted last; SteamVR depth-composites quads that meet (lesson 83). Checked over many panels
 * (flat, tilted up to 60 degrees, either way round), hands and heads:
 *  - every corner of the beam and of the dot is in front of the surface (on the viewer's side) by
 *    at least the dot's lift less a rounding margin: nothing of the pointer reaches the panel;
 *  - the dot lies flat on the surface, over the hit point, facing the viewer;
 *  - the beam runs along the ray from the hand, its face turned toward the head.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "vr_pointer_math.h"

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", what, __LINE__); failures++; } } while (0)

static float frand(float lo, float hi) { return lo + (hi - lo) * (float)rand() / (float)RAND_MAX; }
static float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void quat_axes(const float q[4], float x[3], float y[3], float z[3])
{
    const float qx = q[0], qy = q[1], qz = q[2], qw = q[3];
    x[0] = 1 - 2 * (qy * qy + qz * qz); x[1] = 2 * (qx * qy + qw * qz); x[2] = 2 * (qx * qz - qw * qy);
    y[0] = 2 * (qx * qy - qw * qz); y[1] = 1 - 2 * (qx * qx + qz * qz); y[2] = 2 * (qy * qz + qw * qx);
    z[0] = 2 * (qx * qz + qw * qy); z[1] = 2 * (qy * qz - qw * qx); z[2] = 1 - 2 * (qx * qx + qy * qy);
}

int main(void)
{
    srand(5);
    const float up[3] = { 0, 1, 0 };
    int cases = 0, beams = 0, dots = 0;
    float minBeam = 1e9f, minDot = 1e9f;
    for (int i = 0; i < 20000; i++) {
        /* a panel 0.8-3 m ahead of the head, yawed and tilted, its normal toward the head or away */
        const float head[3] = { frand(-0.3f, 0.3f), frand(1.4f, 1.8f), frand(-0.3f, 0.3f) };
        const float yaw = frand(-0.8f, 0.8f), tilt = frand(-1.05f, 1.05f), dist = frand(0.8f, 3.0f);
        float n[3] = { sinf(yaw) * cosf(tilt), sinf(tilt), cosf(yaw) * cosf(tilt) };   /* toward +z: the head */
        const float h[3] = { head[0] + frand(-0.6f, 0.6f), head[1] + frand(-0.5f, 0.3f), head[2] - dist };
        /* the hand somewhere in front of the panel, near the body */
        const float o[3] = { head[0] + frand(-0.4f, 0.4f), head[1] - frand(0.1f, 0.6f), head[2] - frand(0.1f, 0.5f) };
        const float oDepth = (o[0] - h[0]) * n[0] + (o[1] - h[1]) * n[1] + (o[2] - h[2]) * n[2];
        const float hdDepth = (head[0] - h[0]) * n[0] + (head[1] - h[1]) * n[1] + (head[2] - h[2]) * n[2];
        if (oDepth < 0.1f || hdDepth < 0.1f) {
            continue;   /* the hand or the head behind the panel: no hit in the game either */
        }
        float u[3] = { h[0] - o[0], h[1] - o[1], h[2] - o[2] };
        const float ul = sqrtf(dot(u, u));
        for (int k = 0; k < 3; k++) u[k] /= ul;
        if (-dot(u, n) < 0.15f) {
            continue;   /* grazing: the panel's own hit test gives up long before */
        }
        if (i & 1) {
            n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2];   /* the caller's normal may point either way */
        }
        cases++;
        GevrPtrQuad beam, dq;
        const int got = gevrPointerQuads(o, h, n, head, up, 0.0035f, 0.0225f, &beam, &dq);
        float nv[3] = { n[0], n[1], n[2] };
        if (i & 1) { nv[0] = -nv[0]; nv[1] = -nv[1]; nv[2] = -nv[2]; }
        float c[4][3];
        if (got & 2) {
            dots++;
            gevrPointerQuadCorners(&dq, c);
            for (int k = 0; k < 4; k++) {
                const float d = (c[k][0] - h[0]) * nv[0] + (c[k][1] - h[1]) * nv[1] + (c[k][2] - h[2]) * nv[2];
                if (d < minDot) minDot = d;
                CHECK(d > GEVR_PTR_DOT_LIFT_M - 1e-4f, "the dot is in front of the surface");
            }
            float x[3], y[3], z[3];
            quat_axes(dq.quat, x, y, z);
            CHECK(dot(z, nv) > 0.9999f, "the dot lies flat on the surface, facing the viewer");
            const float off[3] = { dq.pos[0] - h[0], dq.pos[1] - h[1], dq.pos[2] - h[2] };
            const float along = dot(off, nv);
            CHECK(fabsf(along - GEVR_PTR_DOT_LIFT_M) < 1e-4f && dot(off, off) - along * along < 1e-8f,
                  "the dot is over the hit point");
            CHECK(y[1] >= -1e-4f, "the dot's up is the world's up laid on the surface");
        }
        if (got & 1) {
            beams++;
            gevrPointerQuadCorners(&beam, c);
            for (int k = 0; k < 4; k++) {
                const float d = (c[k][0] - h[0]) * nv[0] + (c[k][1] - h[1]) * nv[1] + (c[k][2] - h[2]) * nv[2];
                if (d < minBeam) minBeam = d;
                CHECK(d > GEVR_PTR_BEAM_GAP_M - 1e-4f, "the beam stops in front of the surface");
            }
            float x[3], y[3], z[3];
            quat_axes(beam.quat, x, y, z);
            CHECK(dot(y, u) > 0.9999f, "the beam runs along the ray, hand to hit");
            /* its start at the hand */
            const float s[3] = { beam.pos[0] - y[0] * beam.size[1] * 0.5f, beam.pos[1] - y[1] * beam.size[1] * 0.5f,
                                 beam.pos[2] - y[2] * beam.size[1] * 0.5f };
            CHECK(fabsf(s[0] - o[0]) + fabsf(s[1] - o[1]) + fabsf(s[2] - o[2]) < 1e-4f, "the beam starts at the hand");
            const float th[3] = { head[0] - beam.pos[0], head[1] - beam.pos[1], head[2] - beam.pos[2] };
            CHECK(dot(z, th) >= 0.0f, "the beam faces the head");
        }
    }
    printf("pointer: %d cases, %d beams, %d dots; nearest corner to the surface: beam %.4f m, dot %.4f m\n",
           cases, beams, dots, minBeam, minDot);
    CHECK(dots == cases, "a dot whenever the head is in front of the surface");
    CHECK(beams > cases * 95 / 100, "a beam whenever the hand is clear of the surface");
    if (failures) {
        fprintf(stderr, "test_vr_pointer: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_vr_pointer: ok\n");
    return 0;
}
