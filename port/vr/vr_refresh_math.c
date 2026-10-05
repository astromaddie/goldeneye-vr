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

int gevrRefreshInit(GevrRefresh *r, int setting)
{
    r->setting = gevrRefreshClampSetting(setting);
    r->ceiling = r->setting == GEVR_REFRESH_AUTO ? GEVR_REFRESH_AUTO_HZ : r->setting;
    r->low = 0;
    r->lastFps = 0.0;
    return r->ceiling;
}

int gevrRefreshWindow(GevrRefresh *r, double fps, double displayHz, int worn, double maxGapMs)
{
    if (r->setting != GEVR_REFRESH_AUTO) return 0;   /* a fixed rate is the wearer's choice */
    if (!worn || displayHz <= 1.0) {
        r->low = 0;                                   /* off the head: start counting again */
        return 0;
    }
    if (maxGapMs > GEVR_REFRESH_STALL_MS) return 0;   /* a load: neither low nor good */
    if (fps >= GEVR_REFRESH_LOW_FRACTION * displayHz) {
        r->low = 0;
        return 0;
    }
    r->lastFps = fps;
    if (++r->low < GEVR_REFRESH_LOW_WINDOWS) return 0;
    r->low = 0;
    /* one step below what the display runs at, or below the ceiling if the runtime runs faster */
    const int running = (int)(displayHz + 0.5);
    const int from = running < r->ceiling ? running : r->ceiling;
    const int next = gevrRefreshStepDown(from);
    if (next == 0) return 0;                          /* already at 72: nothing lower */
    r->ceiling = next;
    return next;
}
