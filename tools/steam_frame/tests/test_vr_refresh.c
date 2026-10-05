/*
 * Host test of the display rate's automatic fallback (port/vr/vr_refresh_math.c, the production file
 * vr_openxr.cpp uses). Run: tools/steam_frame/tests/run_tests.sh
 *
 * Standard section 9: refresh_rate defaults to 120 and falls back to 90 and then 72 after two 10 s
 * windows below 90% of the rate while worn. Covers:
 *  - Auto asks for 120; a fixed setting asks for itself and never falls back;
 *  - one low window is not enough; two in a row step 120 -> 90; two more 90 -> 72; nothing below 72;
 *  - a good window between two low ones resets the count;
 *  - windows off the head (not worn) never count and reset the count;
 *  - a window with a load's stall is not counted either way (it neither resets nor adds);
 *  - exactly 90% is not low;
 *  - the runtime running slower than asked (per-app 90 while Auto asks 120): the step is from 90;
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

    /* Auto: 120, then the steps */
    CHECK(gevrRefreshInit(&r, GEVR_REFRESH_AUTO) == 120, "Auto asks for 120");
    CHECK(gevrRefreshWindow(&r, 119.8, 120.0, 1, 20.0) == 0, "a good window");
    CHECK(gevrRefreshWindow(&r, 100.0, 120.0, 1, 20.0) == 0, "one low window is not enough");
    CHECK(gevrRefreshWindow(&r, 101.0, 120.0, 1, 20.0) == 90, "two low windows: 120 -> 90");
    CHECK(gevrRefreshWindow(&r, 80.0, 90.0, 1, 20.0) == 0, "90: one low window");
    CHECK(gevrRefreshWindow(&r, 80.0, 90.0, 1, 20.0) == 72, "90: two low windows -> 72");
    CHECK(gevrRefreshWindow(&r, 50.0, 72.0, 1, 20.0) == 0, "72: low");
    CHECK(gevrRefreshWindow(&r, 50.0, 72.0, 1, 20.0) == 0, "nothing below 72");
    CHECK(r.ceiling == 72, "ceiling 72");

    /* a good window between resets */
    gevrRefreshInit(&r, GEVR_REFRESH_AUTO);
    CHECK(gevrRefreshWindow(&r, 100.0, 120.0, 1, 20.0) == 0, "low");
    CHECK(gevrRefreshWindow(&r, 118.0, 120.0, 1, 20.0) == 0, "good resets");
    CHECK(gevrRefreshWindow(&r, 100.0, 120.0, 1, 20.0) == 0, "low again: only one");
    CHECK(r.low == 1, "count 1 after a reset");

    /* exactly 90% is not low */
    gevrRefreshInit(&r, GEVR_REFRESH_AUTO);
    CHECK(gevrRefreshWindow(&r, 108.0, 120.0, 1, 20.0) == 0, "108 of 120");
    CHECK(gevrRefreshWindow(&r, 108.0, 120.0, 1, 20.0) == 0, "108 of 120 twice: still 120");
    CHECK(r.ceiling == 120, "still 120");

    /* not worn: never counts, and resets */
    gevrRefreshInit(&r, GEVR_REFRESH_AUTO);
    CHECK(gevrRefreshWindow(&r, 30.0, 120.0, 0, 20.0) == 0, "off the head 1");
    CHECK(gevrRefreshWindow(&r, 30.0, 120.0, 0, 20.0) == 0, "off the head 2");
    CHECK(gevrRefreshWindow(&r, 30.0, 120.0, 0, 20.0) == 0, "off the head 3");
    CHECK(r.ceiling == 120 && r.low == 0, "unworn windows leave 120");
    CHECK(gevrRefreshWindow(&r, 100.0, 120.0, 1, 20.0) == 0, "worn low");
    CHECK(gevrRefreshWindow(&r, 100.0, 120.0, 0, 20.0) == 0, "taken off");
    CHECK(gevrRefreshWindow(&r, 100.0, 120.0, 1, 20.0) == 0, "worn low again: count restarted");
    CHECK(r.ceiling == 120, "still 120 after the interruption");

    /* a load's stall: not counted either way */
    gevrRefreshInit(&r, GEVR_REFRESH_AUTO);
    CHECK(gevrRefreshWindow(&r, 100.0, 120.0, 1, 20.0) == 0, "low");
    CHECK(gevrRefreshWindow(&r, 60.0, 120.0, 1, 900.0) == 0, "a load");
    CHECK(r.low == 1, "the load neither reset nor added");
    CHECK(gevrRefreshWindow(&r, 100.0, 120.0, 1, 20.0) == 90, "the next low window steps down");

    /* the runtime runs 90 although Auto asked for 120 (per-app rate 90) */
    gevrRefreshInit(&r, GEVR_REFRESH_AUTO);
    CHECK(gevrRefreshWindow(&r, 89.9, 90.0, 1, 20.0) == 0, "90 held while asking 120");
    CHECK(gevrRefreshWindow(&r, 70.0, 90.0, 1, 20.0) == 0, "low at 90");
    CHECK(gevrRefreshWindow(&r, 70.0, 90.0, 1, 20.0) == 72, "steps from the running 90 to 72");

    /* fixed rates never fall back */
    CHECK(gevrRefreshInit(&r, 120) == 120, "fixed 120");
    for (int i = 0; i < 5; i++) CHECK(gevrRefreshWindow(&r, 60.0, 120.0, 1, 20.0) == 0, "fixed 120 stays");
    CHECK(gevrRefreshInit(&r, 90) == 90, "fixed 90");
    for (int i = 0; i < 5; i++) CHECK(gevrRefreshWindow(&r, 40.0, 90.0, 1, 20.0) == 0, "fixed 90 stays");

    if (failures) {
        fprintf(stderr, "test_vr_refresh: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_vr_refresh: ok\n");
    return 0;
}
