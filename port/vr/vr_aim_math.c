/* See vr_aim_math.h. */
#include "vr_aim_math.h"

#include <math.h>

void gevrQuatMul(const float a[4], const float b[4], float out[4])
{
    out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

static void quatNormalise(float q[4])
{
    const float len = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    int i;
    if (len < 1e-8f) {
        q[0] = q[1] = q[2] = 0.0f;
        q[3] = 1.0f;
        return;
    }
    for (i = 0; i < 4; i++) {
        q[i] /= len;
    }
}

void gevrAimOffsetFromPoses(const float grip[4], const float aim[4], float off[4])
{
    /* Rx(+90): sin 45, cos 45 */
    static const float rx90[4] = { 0.70710678f, 0.0f, 0.0f, 0.70710678f };
    const float conjGrip[4] = { -grip[0], -grip[1], -grip[2], grip[3] };
    float t[4];
    int i;

    gevrQuatMul(conjGrip, aim, t);
    gevrQuatMul(t, rx90, off);
    quatNormalise(off);
    if (off[3] < 0.0f) {
        for (i = 0; i < 4; i++) {
            off[i] = -off[i];
        }
    }
}

void gevrGunQuat(const float grip[4], const float off[4], float out[4])
{
    gevrQuatMul(grip, off, out);
    quatNormalise(out);
}

void gevrGripAxesFromQuat(const float q[4], float right[3], float up[3], float back[3])
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];

    right[0] = 1.0f - 2.0f * (y * y + z * z);
    right[1] = 2.0f * (x * y + w * z);
    right[2] = 2.0f * (x * z - w * y);
    up[0] = -(2.0f * (x * z + w * y));
    up[1] = -(2.0f * (y * z - w * x));
    up[2] = -(1.0f - 2.0f * (x * x + y * y));
    back[0] = 2.0f * (x * y - w * z);
    back[1] = 1.0f - 2.0f * (x * x + z * z);
    back[2] = 2.0f * (y * z + w * x);
}

float gevrQuatAngleDeg(const float q[4])
{
    float w = fabsf(q[3]);
    if (w > 1.0f) {
        w = 1.0f;
    }
    return 2.0f * acosf(w) * (180.0f / 3.14159265f);
}
