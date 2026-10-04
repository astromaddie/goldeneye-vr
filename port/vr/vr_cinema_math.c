/* See vr_cinema_math.h. */
#include "vr_cinema_math.h"
#include "vr_screen_math.h"

#include <math.h>

void gevrCinemaPlace(const float head[3], const float headQuat[4], float aspect, GevrCinemaScreen *out)
{
    GevrScreenPlace p;

    gevrScreenPlaceAhead(head, headQuat, GEVR_CINEMA_DIST_M, GEVR_CINEMA_WIDTH_M, GEVR_CINEMA_DROP_M, 0, &p);
    out->quat[0] = p.quat[0];
    out->quat[1] = p.quat[1];
    out->quat[2] = p.quat[2];
    out->quat[3] = p.quat[3];
    out->yaw = p.yaw;
    out->dist = GEVR_CINEMA_DIST_M;
    out->width = GEVR_CINEMA_WIDTH_M;
    out->picH = 0.0f;
    out->bandH = 0.0f;
    /* p.pos is the picture's centre; gevrCinemaSetAspect moves the rectangle's centre down for the band */
    out->centre[0] = p.pos[0];
    out->centre[1] = p.pos[1];
    out->centre[2] = p.pos[2];
    gevrCinemaSetAspect(out, aspect);
}

void gevrCinemaSetAspect(GevrCinemaScreen *s, float aspect)
{
    const float picCentreY = s->centre[1] + 0.5f * s->bandH;

    if (!(aspect > 0.2f && aspect < 5.0f)) {
        aspect = 4.0f / 3.0f;
    }
    s->picH = s->width / aspect;
    s->bandH = s->picH * GEVR_CINEMA_BAND_FRAC;
    s->centre[1] = picCentreY - 0.5f * s->bandH;
}

int gevrCinemaEyeFov(const GevrCinemaScreen *s, const float eye[3], float fov[4], float *inFront)
{
    /* the eye in the screen's frame: rotate (eye - centre) by the inverse of the yaw */
    const float c = cosf(s->yaw), sn = sinf(s->yaw);
    const float dx = eye[0] - s->centre[0];
    const float dy = eye[1] - s->centre[1];
    const float dz = eye[2] - s->centre[2];
    const float ex = c * dx - sn * dz;
    const float ey = dy;
    const float ez = sn * dx + c * dz;   /* the screen faces +Z: an eye in front has ez > 0 */
    const float halfW = 0.5f * s->width;
    const float halfH = 0.5f * (s->picH + s->bandH);

    if (inFront) {
        *inFront = ez;
    }
    if (!(ez >= 0.1f)) {
        return 0;
    }
    /* atan2 in double: the game links its own atan2f (src/game/math_atan2f.c, 0..2pi, as on the
     * N64) over libm's, which gave the left and down angles as 2pi - a (5.83 rad seen on the Frame) */
    fov[0] = (float)atan2(-halfW - ex, ez);   /* left */
    fov[1] = (float)atan2(halfW - ex, ez);    /* right */
    fov[2] = (float)atan2(halfH - ey, ez);    /* up */
    fov[3] = (float)atan2(-halfH - ey, ez);   /* down */
    return 1;
}

void gevrCinemaShear(float p00, float halfSep, float converge, float out[8])
{
    int eye;

    if (!(converge > 1e-6f)) {
        converge = 1e-6f;
    }
    for (eye = 0; eye < 2; eye++) {
        const float tx = eye == 0 ? -halfSep : halfSep;
        float *o = out + 4 * eye;
        o[0] = tx * p00;
        o[1] = -tx * p00 / converge;
        o[2] = 0.0f;
        o[3] = 0.0f;
    }
}

float gevrCinemaFarParallax(float p00, float halfSepM, float convergeM, float screenW)
{
    return convergeM > 1e-6f ? screenW * p00 * halfSepM / convergeM : 0.0f;
}

float gevrCinemaSafeHalfSep(float p00, float halfSepM, float convergeM, float screenW, float ipdM)
{
    float most;

    if (!(ipdM > 0.04f && ipdM < 0.09f)) {
        ipdM = 0.064f;
    }
    if (!(p00 > 1e-3f) || !(screenW > 1e-3f)) {
        return halfSepM;
    }
    most = GEVR_CINEMA_FAR_MAX * ipdM * convergeM / (screenW * p00);
    return halfSepM < most ? halfSepM : most;
}

float gevrCinemaFadeAlpha(float t)
{
    if (t < 0.0f) {
        return 0.0f;
    }
    if (t < GEVR_CINEMA_HOLD_S) {
        return 1.0f;
    }
    t -= GEVR_CINEMA_HOLD_S;
    if (t >= GEVR_CINEMA_FADE_S) {
        return 0.0f;
    }
    return 1.0f - t / GEVR_CINEMA_FADE_S;
}

int gevrCinemaLatchStep(GevrCinemaLatch *l, int raw, int stereo)
{
    if (stereo) {
        l->on = 0;
        l->quiet = 0;
        return 0;
    }
    if (raw) {
        if (!l->on) {
            l->on = 1;
            l->starts++;
        }
        l->quiet = 0;
        return 1;
    }
    if (l->on && ++l->quiet >= GEVR_CINEMA_RELEASE_FRAMES) {
        l->on = 0;
        l->quiet = 0;
    }
    return l->on;
}
