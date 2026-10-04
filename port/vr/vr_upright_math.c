/* See vr_upright_math.h. */
#include <math.h>

#include "vr_upright_math.h"

static float len3(const float v[3])
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static void cross3(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

int gevrUprightBasis(const float toViewer[3], const float worldUp[3], const float fallbackRight[3],
                     float right[3], float up[3], float back[3])
{
    int ok = 1;
    float n = len3(toViewer);
    if (n < 1e-12f) {
        back[0] = 0.0f;
        back[1] = 0.0f;
        back[2] = 1.0f;
    } else {
        back[0] = toViewer[0] / n;
        back[1] = toViewer[1] / n;
        back[2] = toViewer[2] / n;
    }
    {
        float u = len3(worldUp);
        float w[3] = { 0.0f, 1.0f, 0.0f };
        if (u > 1e-12f) {
            w[0] = worldUp[0] / u;
            w[1] = worldUp[1] / u;
            w[2] = worldUp[2] / u;
        }
        cross3(w, back, right);
    }
    n = len3(right);
    if (n < 1e-3f) {
        /* the face looks straight up or down: keep the given right, square to the face */
        const float d = fallbackRight[0] * back[0] + fallbackRight[1] * back[1] + fallbackRight[2] * back[2];
        right[0] = fallbackRight[0] - d * back[0];
        right[1] = fallbackRight[1] - d * back[1];
        right[2] = fallbackRight[2] - d * back[2];
        n = len3(right);
        ok = 0;
        if (n < 1e-6f) {
            /* any square axis */
            const float a[3] = { fabsf(back[0]) < 0.9f ? 1.0f : 0.0f, fabsf(back[0]) < 0.9f ? 0.0f : 1.0f, 0.0f };
            cross3(a, back, right);
            n = len3(right);
        }
    }
    right[0] /= n;
    right[1] /= n;
    right[2] /= n;
    cross3(back, right, up);
    return ok;
}

void gevrUprightCameraRows(float rows[3][3])
{
    static const float worldUp[3] = { 0.0f, 1.0f, 0.0f };
    const float l0 = len3(rows[0]), l1 = len3(rows[1]);
    float right[3], up[3], back[3];
    gevrUprightBasis(rows[2], worldUp, rows[0], right, up, back);
    for (int i = 0; i < 3; i++) {
        rows[0][i] = right[i] * l0;
        rows[1][i] = up[i] * l1;
    }
}
