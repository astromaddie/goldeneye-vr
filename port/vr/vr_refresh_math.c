/* See vr_refresh_math.h. */
#include "vr_refresh_math.h"

int gevrRefreshClampSetting(int hz)
{
    if (hz <= 0) return GEVR_REFRESH_AUTO;
    if (hz >= 105) return 120;
    if (hz >= 81) return 90;
    return 72;
}

int gevrRefreshStepDown(int hz)
{
    if (hz > 100) return 90;
    if (hz > 80) return 72;
    return 0;
}

int gevrRefreshRequestFor(int setting, const float *offered, int nOffered)
{
    if (setting <= 0) return 0;
    for (int i = 0; i < nOffered; i++) {
        const float d = offered[i] - (float)setting;
        if (d > -0.5f && d < 0.5f) return setting;
    }
    return 0;
}

void gevrRefreshInit(GevrRefresh *r)
{
    r->low = 0;
    r->recommended = 0;
    r->fps[0] = r->fps[1] = 0.0;
}

int gevrRefreshWindow(GevrRefresh *r, double fps, double displayHz, int worn, double maxGapMs)
{
    if (r->recommended) return 0;                     /* once a session */
    if (!worn || displayHz <= 1.0) {
        r->low = 0;                                   /* off the head: start counting again */
        return 0;
    }
    if (maxGapMs > GEVR_REFRESH_STALL_MS) return 0;   /* a load: neither low nor good */
    if (fps >= GEVR_REFRESH_LOW_FRACTION * displayHz) {
        r->low = 0;
        return 0;
    }
    r->fps[0] = r->fps[1];
    r->fps[1] = fps;
    if (++r->low < GEVR_REFRESH_LOW_WINDOWS) return 0;
    const int next = gevrRefreshStepDown((int)(displayHz + 0.5));
    r->recommended = next > 0 ? next : -1;            /* -1: already at 72, logged once as such */
    return r->recommended;
}
