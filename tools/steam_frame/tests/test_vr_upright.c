/*
 * Host test of the upright quads (port/vr/vr_upright_math.c, the production file the billboards,
 * the 3D sight and the HUD quads use). Run: tools/steam_frame/tests/run_tests.sh
 *
 * Wear finding 2026-10-04: the crosshair and the billboard sprites rolled with a tilted head.
 * Covers:
 *  - a camera rolled 20 degrees: the billboard's right axis is horizontal (world y 0), its up is
 *    upward, its face still the camera's back; with no roll it is exactly the camera's own axes;
 *  - pitched 50 degrees down and rolled: still horizontal, still facing the camera;
 *  - looking straight down: the camera's right is kept (no spin, no NaN);
 *  - row lengths (the game's scaled view-to-world matrix) are kept;
 *  - in view space with the head rolled (world up turned into the view): the sight's right, taken
 *    back to the world, is horizontal.
 */
#include <math.h>
#include <stdio.h>

#include "vr_upright_math.h"

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", what, __LINE__); failures++; } } while (0)
#define NEAR(a, b, tol) (fabsf((a) - (b)) <= (tol))

/* a camera's world rows (right, up, back) for yaw, pitch (down negative), roll (right ear down positive) */
static void cameraRows(float yawDeg, float pitchDeg, float rollDeg, float rows[3][3])
{
    const float y = yawDeg / 57.29578f, p = pitchDeg / 57.29578f, r = rollDeg / 57.29578f;
    /* R = Ry(yaw) Rx(pitch) Rz(-roll); rows are R's columns */
    const float cy = cosf(y), sy = sinf(y), cp = cosf(p), sp = sinf(p), cr = cosf(-r), sr = sinf(-r);
    const float Ry[3][3] = { { cy, 0, sy }, { 0, 1, 0 }, { -sy, 0, cy } };
    const float Rx[3][3] = { { 1, 0, 0 }, { 0, cp, -sp }, { 0, sp, cp } };
    const float Rz[3][3] = { { cr, -sr, 0 }, { sr, cr, 0 }, { 0, 0, 1 } };
    float A[3][3], R[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            A[i][j] = 0;
            for (int k = 0; k < 3; k++) A[i][j] += Ry[i][k] * Rx[k][j];
        }
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            R[i][j] = 0;
            for (int k = 0; k < 3; k++) R[i][j] += A[i][k] * Rz[k][j];
        }
    for (int c = 0; c < 3; c++)
        for (int i = 0; i < 3; i++) rows[c][i] = R[i][c];
}

static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

int main(void)
{
    /* rolled 20 degrees: the camera's right tilts, the billboard's does not */
    {
        float rows[3][3];
        cameraRows(30.0f, 0.0f, 20.0f, rows);
        CHECK(fabsf(rows[0][1]) > 0.3f, "the rolled camera's own right is tilted (the old billboard)");
        float b[3][3];
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) b[i][j] = rows[i][j];
        gevrUprightCameraRows(b);
        printf("roll 20: camera right.y %.3f, billboard right.y %.3f, up.y %.3f\n", rows[0][1], b[0][1], b[1][1]);
        CHECK(NEAR(b[0][1], 0.0f, 1e-6f), "the billboard's right is horizontal");
        CHECK(b[1][1] > 0.99f, "its up is world up (no pitch)");
        CHECK(NEAR(dot3(b[0], rows[2]), 0.0f, 1e-6f) && NEAR(dot3(b[1], rows[2]), 0.0f, 1e-6f),
              "it still faces the camera");
        CHECK(dot3(b[0], rows[0]) > 0.9f, "and its right is on the camera's right side");
    }
    /* no roll: unchanged */
    {
        float rows[3][3], b[3][3];
        cameraRows(-75.0f, -20.0f, 0.0f, rows);
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) b[i][j] = rows[i][j];
        gevrUprightCameraRows(b);
        int same = 1;
        for (int i = 0; i < 2; i++) for (int j = 0; j < 3; j++) same &= NEAR(b[i][j], rows[i][j], 1e-5f);
        CHECK(same, "without roll the billboard is the camera's own axes");
    }
    /* pitched down 50 and rolled 25 */
    {
        float rows[3][3], cam[3][3];
        cameraRows(120.0f, -50.0f, -25.0f, rows);
        cameraRows(120.0f, -50.0f, -25.0f, cam);
        gevrUprightCameraRows(rows);
        CHECK(NEAR(rows[0][1], 0.0f, 1e-6f) && rows[1][1] > 0.0f, "pitched and rolled: right horizontal, up upward");
        CHECK(NEAR(dot3(rows[0], cam[2]), 0.0f, 1e-6f) && NEAR(dot3(rows[1], cam[2]), 0.0f, 1e-6f),
              "pitched and rolled: still facing the camera");
    }
    /* straight down, rolled: the camera's right is kept */
    {
        float rows[3][3], b[3][3];
        cameraRows(10.0f, -90.0f, 35.0f, rows);
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) b[i][j] = rows[i][j];
        gevrUprightCameraRows(b);
        CHECK(dot3(b[0], rows[0]) > 0.999f && !isnan(b[1][0]), "looking straight down keeps the camera's right");
    }
    /* scaled rows (view-to-world with 1 / 0.2) keep their lengths */
    {
        float rows[3][3];
        cameraRows(0.0f, 0.0f, 20.0f, rows);
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) rows[i][j] *= 5.0f;
        gevrUprightCameraRows(rows);
        CHECK(NEAR(sqrtf(dot3(rows[0], rows[0])), 5.0f, 1e-4f) && NEAR(sqrtf(dot3(rows[1], rows[1])), 5.0f, 1e-4f),
              "row lengths kept");
    }
    /* the sight in view space: head rolled 20, world up turned into the view; a point ahead */
    {
        float rows[3][3];
        cameraRows(0.0f, 10.0f, 20.0f, rows);   /* view-to-world: rows are the view axes in the world */
        /* world up in view space: (rows[0].y, rows[1].y, rows[2].y) */
        const float upV[3] = { rows[0][1], rows[1][1], rows[2][1] };
        const float p[3] = { 0.1f, -0.05f, -3.0f };
        const float toViewer[3] = { -p[0], -p[1], -p[2] };
        const float camRight[3] = { 1.0f, 0.0f, 0.0f };
        float r[3], u[3], b[3];
        gevrUprightBasis(toViewer, upV, camRight, r, u, b);
        /* back to the world */
        float rw[3], uw[3];
        for (int i = 0; i < 3; i++) {
            rw[i] = r[0] * rows[0][i] + r[1] * rows[1][i] + r[2] * rows[2][i];
            uw[i] = u[0] * rows[0][i] + u[1] * rows[1][i] + u[2] * rows[2][i];
        }
        printf("sight, head rolled 20: right in the world (%.3f %.3f %.3f), up.y %.3f\n", rw[0], rw[1], rw[2], uw[1]);
        CHECK(NEAR(rw[1], 0.0f, 1e-5f) && uw[1] > 0.9f, "the sight keeps world up with the head rolled");
        CHECK(NEAR(dot3(r, p), 0.0f, 1e-5f) && NEAR(dot3(u, p), 0.0f, 1e-5f), "and faces the eye");
    }

    if (failures) {
        fprintf(stderr, "test_vr_upright: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_vr_upright: ok\n");
    return 0;
}
