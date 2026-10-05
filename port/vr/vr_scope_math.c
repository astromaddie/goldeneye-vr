/* See vr_scope_math.h. */
#include <math.h>
#include <string.h>

#include "vr_scope_math.h"

static float dot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void gevrScopeDisc(const float lens[3], float radius, const float right[3], const float up[3],
                   const float axis[3], GevrScopeDisc *out)
{
    memset(out, 0, sizeof(*out));
    memcpy(out->lens, lens, sizeof(out->lens));
    memcpy(out->right, right, sizeof(out->right));
    memcpy(out->up, up, sizeof(out->up));
    memcpy(out->axis, axis, sizeof(out->axis));
    out->radius = radius;
    /* the eyepiece's face looks back along the gun: seen only from behind it */
    if (!(radius > 0.0f) || !(dot3(lens, axis) > 0.0f)) {
        return;
    }
    float l = 1e30f, r = -1e30f, u = -1e30f, d = 1e30f;
    for (int i = 0; i < GEVR_SCOPE_DISC_STEPS; i++) {
        const float a = (float)i * (6.28318530718f / (float)GEVR_SCOPE_DISC_STEPS);
        const float c = cosf(a) * radius, s = sinf(a) * radius;
        const float p[3] = { lens[0] + right[0] * c + up[0] * s, lens[1] + right[1] * c + up[1] * s,
                             lens[2] + right[2] * c + up[2] * s };
        if (!(-p[2] > GEVR_SCOPE_DISC_MIN_Z_M)) {
            return;   /* the disc reaches the eye's plane: the model is cut there too */
        }
        const float x = p[0] / -p[2], y = p[1] / -p[2];
        l = x < l ? x : l;
        r = x > r ? x : r;
        d = y < d ? y : d;
        u = y > u ? y : u;
    }
    /* the sampled outline is a polygon inside the ellipse: pad the box */
    const float pw = (r - l) * GEVR_SCOPE_DISC_PAD, ph = (u - d) * GEVR_SCOPE_DISC_PAD;
    out->box[0] = l - pw;
    out->box[1] = r + pw;
    out->box[2] = u + ph;
    out->box[3] = d - ph;
    out->visible = 1;
}

void gevrScopeViewBasis(const float lens[3], float x[3], float y[3], float z[3])
{
    const float len = sqrtf(dot3(lens, lens));
    if (!(len > 1e-6f)) {
        x[0] = 1; x[1] = 0; x[2] = 0;
        y[0] = 0; y[1] = 1; y[2] = 0;
        z[0] = 0; z[1] = 0; z[2] = 1;
        return;
    }
    z[0] = -lens[0] / len; z[1] = -lens[1] / len; z[2] = -lens[2] / len;
    /* x = up x z, level */
    x[0] = z[2]; x[1] = 0.0f; x[2] = -z[0];
    float xl = sqrtf(x[0] * x[0] + x[2] * x[2]);
    if (xl < 1e-4f) {
        /* straight up or down: keep the eye's own x */
        x[0] = 1.0f; x[1] = 0.0f; x[2] = 0.0f;
        const float k = dot3(x, z);
        x[0] -= z[0] * k; x[1] -= z[1] * k; x[2] -= z[2] * k;
        xl = sqrtf(dot3(x, x));
    }
    x[0] /= xl; x[1] /= xl; x[2] /= xl;
    y[0] = z[1] * x[2] - z[2] * x[1];
    y[1] = z[2] * x[0] - z[0] * x[2];
    y[2] = z[0] * x[1] - z[1] * x[0];
}

int gevrScopeAimEye(const GevrScopeDisc eyes[2], float *cosOut)
{
    int best = -1;
    float bestLen = 1e30f;
    for (int e = 0; e < 2; e++) {
        if (!eyes[e].visible) {
            continue;
        }
        const float len = sqrtf(dot3(eyes[e].lens, eyes[e].lens));
        if (len < bestLen) {
            bestLen = len;
            best = e;
        }
    }
    if (cosOut) {
        *cosOut = best >= 0 && bestLen > 1e-6f ? dot3(eyes[best].lens, eyes[best].axis) / bestLen : 0.0f;
    }
    return best;
}

float gevrScopeDiscTan(const GevrScopeDisc *d)
{
    const float len = sqrtf(dot3(d->lens, d->lens));
    return d->visible && len > 1e-4f ? d->radius / len : 0.0f;
}

int gevrScopeDiscTexcoord(const GevrScopeDisc *d, const float tan[2], float tex[2], float *rim)
{
    if (!d->visible) {
        return 0;
    }
    const float dir[3] = { tan[0], tan[1], -1.0f };
    const float den = dot3(dir, d->axis);
    if (!(fabsf(den) > 1e-6f)) {
        return 0;
    }
    const float s = dot3(d->lens, d->axis) / den;   /* the ray meets the eyepiece's plane at s dir */
    if (!(s > 0.0f)) {
        return 0;
    }
    const float h[3] = { s * dir[0] - d->lens[0], s * dir[1] - d->lens[1], s * dir[2] - d->lens[2] };
    const float a = dot3(h, d->right) / d->radius, b = dot3(h, d->up) / d->radius;
    const float r = sqrtf(a * a + b * b);
    if (r > 1.0f) {
        return 0;
    }
    tex[0] = a * 0.5f + 0.5f;
    tex[1] = b * 0.5f + 0.5f;
    if (rim) {
        *rim = r;
    }
    return 1;
}

int gevrScopeCameraMatrix(const float origin[3], const float dir[3], const float up[3], float fovDeg,
                          float nearZ, float farZ, float vp[16])
{
    float f[3] = { dir[0], dir[1], dir[2] };
    const float fl = sqrtf(dot3(f, f));
    if (!(fl > 1e-6f)) {
        return 0;
    }
    f[0] /= fl; f[1] /= fl; f[2] /= fl;
    /* right = f x up, up = right x f */
    float r[3] = { f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0] };
    const float len = sqrtf(dot3(r, r));
    if (len < 1e-4f) {
        return 0;
    }
    r[0] /= len; r[1] /= len; r[2] /= len;
    const float u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
    const float g = 1.0f / tanf(fovDeg * 0.5f * (3.14159265358979f / 180.0f));
    const float A = (farZ + nearZ) / (nearZ - farZ);
    const float B = 2.0f * farZ * nearZ / (nearZ - farZ);
    const float ro = dot3(r, origin), uo = dot3(u, origin), fo = dot3(f, origin);
    /* rows: g x right, g x up, the depth row, and w = the distance along f */
    for (int i = 0; i < 3; i++) {
        vp[i * 4 + 0] = g * r[i];
        vp[i * 4 + 1] = g * u[i];
        vp[i * 4 + 2] = -A * f[i];
        vp[i * 4 + 3] = f[i];
    }
    vp[12] = -g * ro;
    vp[13] = -g * uo;
    vp[14] = A * fo + B;
    vp[15] = -fo;
    return 1;
}
