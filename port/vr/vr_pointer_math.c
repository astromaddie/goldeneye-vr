/* See vr_pointer_math.h. */
#include <math.h>
#include <string.h>

#include "vr_pointer_math.h"

static float dot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void cross3(const float a[3], const float b[3], float o[3])
{
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

static int norm3(float v[3])
{
    const float l = sqrtf(dot3(v, v));
    if (!(l > 1e-6f)) {
        return 0;
    }
    v[0] /= l;
    v[1] /= l;
    v[2] /= l;
    return 1;
}

/* the quaternion of the rotation whose columns are x, y, z (orthonormal, right-handed) */
static void quat_from_basis(const float x[3], const float y[3], const float z[3], float q[4])
{
    const float m00 = x[0], m01 = y[0], m02 = z[0];
    const float m10 = x[1], m11 = y[1], m12 = z[1];
    const float m20 = x[2], m21 = y[2], m22 = z[2];
    const float tr = m00 + m11 + m22;
    if (tr > 0.0f) {
        const float s = sqrtf(tr + 1.0f) * 2.0f;
        q[3] = 0.25f * s; q[0] = (m21 - m12) / s; q[1] = (m02 - m20) / s; q[2] = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
        q[3] = (m21 - m12) / s; q[0] = 0.25f * s; q[1] = (m01 + m10) / s; q[2] = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
        q[3] = (m02 - m20) / s; q[0] = (m01 + m10) / s; q[1] = 0.25f * s; q[2] = (m12 + m21) / s;
    } else {
        const float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
        q[3] = (m10 - m01) / s; q[0] = (m02 + m20) / s; q[1] = (m12 + m21) / s; q[2] = 0.25f * s;
    }
}

int gevrPointerQuads(const float o[3], const float h[3], const float nIn[3], const float head[3],
                     const float worldUp[3], float beamHalf, float dotRadius,
                     GevrPtrQuad *beam, GevrPtrQuad *dot)
{
    int out = 0;
    float n[3] = { nIn[0], nIn[1], nIn[2] };
    if (!norm3(n)) {
        return 0;
    }
    /* the normal toward the viewer */
    const float toHead[3] = { head[0] - h[0], head[1] - h[1], head[2] - h[2] };
    if (dot3(n, toHead) < 0.0f) {
        n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2];
    }
    memset(beam, 0, sizeof(*beam));
    memset(dot, 0, sizeof(*dot));

    /* the dot: flat on the surface, lifted toward the viewer, the world's up laid onto the surface */
    if (dot3(n, toHead) > 0.0f) {
        float y[3];
        const float k = dot3(worldUp, n);
        y[0] = worldUp[0] - n[0] * k; y[1] = worldUp[1] - n[1] * k; y[2] = worldUp[2] - n[2] * k;
        if (!norm3(y)) {
            /* the surface faces straight up or down: any direction in it */
            const float alt[3] = { 0.0f, 0.0f, 1.0f };
            const float k2 = dot3(alt, n);
            y[0] = alt[0] - n[0] * k2; y[1] = alt[1] - n[1] * k2; y[2] = alt[2] - n[2] * k2;
            norm3(y);
        }
        float x[3];
        cross3(y, n, x);
        for (int i = 0; i < 3; i++) {
            dot->pos[i] = h[i] + n[i] * GEVR_PTR_DOT_LIFT_M;
        }
        quat_from_basis(x, y, n, dot->quat);
        dot->size[0] = dot->size[1] = 2.0f * dotRadius;
        out |= 2;
    }

    /* the beam: from the hand to GEVR_PTR_BEAM_GAP_M in front of the surface along the ray */
    float u[3] = { h[0] - o[0], h[1] - o[1], h[2] - o[2] };
    const float len = sqrtf(dot3(u, u));
    if (len > 1e-4f) {
        u[0] /= len; u[1] /= len; u[2] /= len;
        const float hDepth = dot3((const float[3]){ o[0] - h[0], o[1] - h[1], o[2] - h[2] }, n);   /* the hand's height over the surface */
        const float c = -dot3(u, n);   /* how square the ray meets the surface (1 = head on) */
        /* back along the ray until the end clears the surface by the gap (and the strip's half width) */
        const float clear = GEVR_PTR_BEAM_GAP_M + beamHalf;
        const float back = c > 0.05f ? clear / c : clear / 0.05f;
        const float keep = len - back;
        if (hDepth > clear + 0.01f && keep > 0.02f) {
            const float e[3] = { o[0] + u[0] * keep, o[1] + u[1] * keep, o[2] + u[2] * keep };
            const float mid[3] = { (o[0] + e[0]) * 0.5f, (o[1] + e[1]) * 0.5f, (o[2] + e[2]) * 0.5f };
            /* face the head about the beam's own line */
            float z[3] = { head[0] - mid[0], head[1] - mid[1], head[2] - mid[2] };
            const float kz = dot3(z, u);
            z[0] -= u[0] * kz; z[1] -= u[1] * kz; z[2] -= u[2] * kz;
            if (!norm3(z)) {
                z[0] = n[0]; z[1] = n[1]; z[2] = n[2];
                const float kn = dot3(z, u);
                z[0] -= u[0] * kn; z[1] -= u[1] * kn; z[2] -= u[2] * kn;
                norm3(z);
            }
            float x[3];
            cross3(u, z, x);
            memcpy(beam->pos, mid, sizeof(mid));
            quat_from_basis(x, u, z, beam->quat);
            beam->size[0] = 2.0f * beamHalf;
            beam->size[1] = keep;
            out |= 1;
        }
    }
    return out;
}

void gevrPointerQuadCorners(const GevrPtrQuad *q, float corners[4][3])
{
    /* rotate the quad's local +X and +Y by its quaternion */
    const float qx = q->quat[0], qy = q->quat[1], qz = q->quat[2], qw = q->quat[3];
    const float ax[3] = { 1.0f - 2.0f * (qy * qy + qz * qz), 2.0f * (qx * qy + qw * qz), 2.0f * (qx * qz - qw * qy) };
    const float ay[3] = { 2.0f * (qx * qy - qw * qz), 1.0f - 2.0f * (qx * qx + qz * qz), 2.0f * (qy * qz + qw * qx) };
    static const float sx[4] = { 1.0f, -1.0f, -1.0f, 1.0f }, sy[4] = { 1.0f, 1.0f, -1.0f, -1.0f };
    for (int c = 0; c < 4; c++) {
        for (int i = 0; i < 3; i++) {
            corners[c][i] = q->pos[i] + ax[i] * sx[c] * q->size[0] * 0.5f + ay[i] * sy[c] * q->size[1] * 0.5f;
        }
    }
}
