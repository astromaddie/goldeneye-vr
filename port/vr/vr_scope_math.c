/* See vr_scope_math.h. */
#include <math.h>

#include "vr_scope_math.h"

void gevrScopeEyeAperture(const float lens[3], float radius, const float right[3], const float up[3],
                          const float axis[3], const float eye[3], GevrScopeEyeView *out)
{
    const float d[3] = { lens[0] - eye[0], lens[1] - eye[1], lens[2] - eye[2] };
    const float depth = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];
    out->depth = depth;
    if (!(depth > GEVR_SCOPE_EYE_MIN_DEPTH_M) || !(radius > 0.0f)) {
        out->visible = 0;
        out->center[0] = out->center[1] = 0.0f;
        out->radius = 0.0f;
        return;
    }
    out->visible = 1;
    out->center[0] = (d[0] * right[0] + d[1] * right[1] + d[2] * right[2]) / depth;
    out->center[1] = (d[0] * up[0] + d[1] * up[1] + d[2] * up[2]) / depth;
    out->radius = radius / depth;
}

float gevrScopeField(const GevrScopeEyeView eyes[2], int *nearest)
{
    int near = -1;
    float nearOff = 0.0f, all = 0.0f;
    for (int e = 0; e < 2; e++) {
        if (!eyes[e].visible) {
            continue;
        }
        const float off = sqrtf(eyes[e].center[0] * eyes[e].center[0] + eyes[e].center[1] * eyes[e].center[1]);
        if (near < 0 || off < nearOff) {
            near = e;
            nearOff = off;
        }
        if (off + eyes[e].radius > all) {
            all = off + eyes[e].radius;
        }
    }
    if (nearest) {
        *nearest = near;
    }
    if (near < 0) {
        return 0.0f;
    }
    const float needNear = nearOff + eyes[near].radius;
    const float cap = needNear > GEVR_SCOPE_FIELD_CAP ? needNear : GEVR_SCOPE_FIELD_CAP;
    float field = all < cap ? all : cap;
    if (field > GEVR_SCOPE_FIELD_MAX) {
        field = GEVR_SCOPE_FIELD_MAX;
    }
    if (field < 1e-3f) {
        field = 1e-3f;
    }
    return field;
}

void gevrScopeEyeFovTan(const GevrScopeEyeView *v, float fovTan[4])
{
    const float r = v->visible ? v->radius : 0.01f;
    const float cx = v->visible ? v->center[0] : 0.0f, cy = v->visible ? v->center[1] : 0.0f;
    fovTan[0] = cx - r;
    fovTan[1] = cx + r;
    fovTan[2] = cy + r;
    fovTan[3] = cy - r;
}

int gevrScopeEyeTexcoord(const GevrScopeEyeView *v, float field, const float q[2], float tex[2])
{
    if (!v->visible || q[0] * q[0] + q[1] * q[1] > 1.0f || !(field > 0.0f)) {
        return 0;
    }
    const float t[2] = { v->center[0] + q[0] * v->radius, v->center[1] + q[1] * v->radius };
    tex[0] = t[0] / field * 0.5f + 0.5f;
    tex[1] = t[1] / field * 0.5f + 0.5f;
    return t[0] * t[0] + t[1] * t[1] > field * field ? 2 : 1;
}
