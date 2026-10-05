/*
 * Host test of the display rate's handling (port/vr/vr_refresh_math.c, the production file vr_openxr.cpp
 * uses). Run: tools/steam_frame/tests/run_tests.sh
 *
 * Standard section 9 (as corrected 2026-10-05): the app can't change the Frame's rate, so no fallback
 * ladder; it asks at most once, only for an offered rate, and after two 10 s windows below 90% of the
 * display rate while worn it recommends, once, the per-app rate to set. Covers:
 *  - the request: Auto asks for nothing; a fixed rate only when offered;
 *  - one low window is not enough; two in a row recommend one step down (120 -> 90, 90 -> 72);
 *    at 72, -1 (nothing lower); only once a session;
 *  - a good window between two low ones resets the count;
 *  - windows off the head (not worn) never count and reset the count;
 *  - a window with a load's stall is not counted either way;
 *  - exactly 90% is not low;
 *  - DisplayHz clamping: 0 and negative Auto, 60 -> 72, 100 -> 90, 144 -> 120.
 */
#include <stdio.h>

#include "vr_refresh_math.h"

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", what, __LINE__); failures++; } } while (0)

int main(void)
{
    GevrRefresh r;

    CHECK(gevrRefreshClampSetting(0) == GEVR_REFRESH_AUTO, "0 is Auto");
    CHECK(gevrRefreshClampSetting(-5) == GEVR_REFRESH_AUTO, "negative is Auto");
    CHECK(gevrRefreshClampSetting(60) == 72, "60 -> 72");
    CHECK(gevrRefreshClampSetting(72) == 72, "72");
    CHECK(gevrRefreshClampSetting(90) == 90, "90");
    CHECK(gevrRefreshClampSetting(100) == 90, "100 -> 90");
    CHECK(gevrRefreshClampSetting(120) == 120, "120");
    CHECK(gevrRefreshClampSetting(144) == 120, "144 -> 120");

    /* the request */
    const float only120[1] = { 120.0f }, only90[1] = { 90.0f };
    CHECK(gevrRefreshRequestFor(GEVR_REFRESH_AUTO, only120, 1) == 0, "Auto asks for nothing");
    CHECK(gevrRefreshRequestFor(120, only120, 1) == 120, "120 offered: asked for");
    CHECK(gevrRefreshRequestFor(120, only90, 1) == 0, "120 not offered: not asked");
    CHECK(gevrRefreshRequestFor(90, only90, 1) == 90, "90 offered");
    CHECK(gevrRefreshRequestFor(90, NULL, 0) == 0, "nothing offered");

    /* the recommendation: two low worn windows, once */
    gevrRefreshInit(&r);
    CHECK(gevrRefreshWindow(&r, 119.8, 120.0, 1, 20.0) == 0, "a good window");
    CHECK(gevrRefreshWindow(&r, 100.0, 120.0, 1, 20.0) == 0, "one low window is not enough");
    CHECK(gevrRefreshWindow(&r, 101.0, 120.0, 1, 20.0) == 90, "two low windows at 120: recommend 90");
    CHECK(r.fps[0] == 100.0 && r.fps[1] == 101.0, "the two windows' fps kept for the log");
    CHECK(gevrRefreshWindow(&r, 100.0, 120.0, 1, 20.0) == 0, "only once");
    CHECK(gevrRefreshWindow(&r, 100.0, 120.0, 1, 20.0) == 0, "still only once");

    gevrRefreshInit(&r);
    gevrRefreshWindow(&r, 70.0, 90.0, 1, 20.0);
    CHECK(gevrRefreshWindow(&r, 70.0, 90.0, 1, 20.0) == 72, "at 90: recommend 72");
    gevrRefreshInit(&r);
    gevrRefreshWindow(&r, 50.0, 72.0, 1, 20.0);
    CHECK(gevrRefreshWindow(&r, 50.0, 72.0, 1, 20.0) == -1, "at 72: nothing lower (-1)");

    /* a good window between resets */
    gevrRefreshInit(&r);
    CHECK(gevrRefreshWindow(&r, 100.0, 120.0, 1, 20.0) == 0, "low");
    CHECK(gevrRefreshWindow(&r, 118.0, 120.0, 1, 20.0) == 0, "good resets");
    CHECK(gevrRefreshWindow(&r, 100.0, 120.0, 1, 20.0) == 0, "low again: only one");
    CHECK(r.low == 1, "count 1 after a reset");

    /* exactly 90% is not low */
    gevrRefreshInit(&r);
    CHECK(gevrRefreshWindow(&r, 108.0, 120.0, 1, 20.0) == 0, "108 of 120");
    CHECK(gevrRefreshWindow(&r, 108.0, 120.0, 1, 20.0) == 0, "108 of 120 twice: fine");

    /* not worn: never counts, and resets */
    gevrRefreshInit(&r);
    for (int i = 0; i < 3; i++) CHECK(gevrRefreshWindow(&r, 30.0, 120.0, 0, 20.0) == 0, "off the head");
    CHECK(r.low == 0, "unworn windows don't count");
    CHECK(gevrRefreshWindow(&r, 100.0, 120.0, 1, 20.0) == 0, "worn low");
    CHECK(gevrRefreshWindow(&r, 100.0, 120.0, 0, 20.0) == 0, "taken off");
    CHECK(gevrRefreshWindow(&r, 100.0, 120.0, 1, 20.0) == 0, "worn low again: count restarted");

    /* a load's stall: not counted either way */
    gevrRefreshInit(&r);
    CHECK(gevrRefreshWindow(&r, 100.0, 120.0, 1, 20.0) == 0, "low");
    CHECK(gevrRefreshWindow(&r, 60.0, 120.0, 1, 900.0) == 0, "a load");
    CHECK(r.low == 1, "the load neither reset nor added");
    CHECK(gevrRefreshWindow(&r, 100.0, 120.0, 1, 20.0) == 90, "the next low window recommends");

    if (failures) {
        fprintf(stderr, "test_vr_refresh: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_vr_refresh: ok\n");
    return 0;
}
