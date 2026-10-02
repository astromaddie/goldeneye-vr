/*
 * Host test of the Steam Frame system layer (port/vr/vr_system.cpp, the production file) with
 * sfvr's real state machine: a fake controller, haptics, game and clock. Run: tools/steam_frame/tests/run_tests.sh
 *
 * Covers standard.md section 1: View short press = back (and only that), hold 1 s = recentre + buzz on
 * both hands, hold 3 s = recentre + standing height + buzz again, one event per press, a press cut off by
 * focus loss or by another profile does nothing, and a button already down when the session starts is ignored.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <openxr/openxr.h>

#include "../../../port/vr/vr_system.h"

static int s_profile = 1, s_view = 0, s_floor = 1;
static int s_recentreCalls, s_lastFlags, s_screenRecentres, s_buzzes[2];
static double s_buzzAmp[2], s_buzzSec[2];
static double s_fakeNow;

extern "C" {
int gevrVrFrameProfile(void) { return s_profile; }
bool get_button_state(int hand, const char *name)
{
    return hand == 0 && !strcmp(name, "view") && s_view;
}
XrResult trigger_haptic_vibration_c(int hand, float amp, float sec)
{
    s_buzzes[hand]++;
    s_buzzAmp[hand] = amp;
    s_buzzSec[hand] = sec;
    return XR_SUCCESS;
}
void vr_screen_recenter(void) { s_screenRecentres++; }
int gevrVrFloorRelative(void) { return s_floor; }
void gevrRequestRecenter(int flags) { s_recentreCalls++; s_lastFlags = flags; }
}

/* vr_system.cpp reads the real clock; the test sleeps in real (short) time through this helper. */
static void wait_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, nullptr);
}

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", what, __LINE__); failures++; } } while (0)

static void reset_counts()
{
    s_recentreCalls = s_lastFlags = s_screenRecentres = 0;
    s_buzzes[0] = s_buzzes[1] = 0;
}

/* press for `ms`, calling vrSystemFrame every ~10 ms like an XR frame, then release */
static void press(int ms, int focused = 1)
{
    s_view = 1;
    for (int t = 0; t < ms; t += 10) { vrSystemFrame(focused); wait_ms(10); }
    s_view = 0;
    vrSystemFrame(focused);
}

int main()
{
    (void)s_fakeNow;
    /* a button already down at the first frame is ignored (not a press we saw start) */
    s_view = 1; vrSystemFrame(1); wait_ms(30); s_view = 0; vrSystemFrame(1);
    CHECK(!gevrSystemBack() && s_recentreCalls == 0, "a press already down at the start is ignored");

    /* short press: back, nothing else */
    reset_counts();
    press(120);
    CHECK(gevrSystemBack() == 1, "short press gives the game a Back");
    CHECK(s_recentreCalls == 0 && s_buzzes[0] == 0, "short press does not recentre or buzz");
    wait_ms(200);
    CHECK(gevrSystemBack() == 0, "the Back pulse is short (about 150 ms)");

    /* hold 1.2 s: recentre once, buzz both hands (0.6 / 80 ms), no Back */
    reset_counts();
    press(1200);
    CHECK(s_recentreCalls == 1 && s_lastFlags == 1, "hold 1 s recentres (flags 1)");
    CHECK(s_screenRecentres == 1, "hold 1 s re-places the world-locked panels");
    CHECK(s_buzzes[0] == 1 && s_buzzes[1] == 1, "hold 1 s buzzes both hands once");
    CHECK(s_buzzAmp[0] > 0.59f && s_buzzAmp[0] < 0.61f && s_buzzSec[0] > 0.079f && s_buzzSec[0] < 0.081f, "the buzz is 0.6 for 80 ms");
    CHECK(gevrSystemBack() == 0, "a held View is not also a Back");

    /* hold 3.2 s: recentre, then recentre + standing height, two buzzes */
    reset_counts();
    press(3200);
    CHECK(s_recentreCalls == 2 && s_lastFlags == 2, "hold 3 s ends with recentre + standing height (flags 2)");
    CHECK(s_buzzes[0] == 2 && s_buzzes[1] == 2, "hold 3 s buzzes again");

    /* focus lost mid-press: cancelled, no events, and the release is not a Back */
    reset_counts();
    s_view = 1;
    for (int t = 0; t < 600; t += 10) { vrSystemFrame(1); wait_ms(10); }
    for (int t = 0; t < 1500; t += 10) { vrSystemFrame(0); wait_ms(10); }   /* focus gone: not FOCUSED */
    s_view = 0; vrSystemFrame(0);
    vrSystemFrame(1);
    CHECK(s_recentreCalls == 0 && gevrSystemBack() == 0, "focus loss cancels a press without events");

    /* another profile (Touch, Index): View does not exist, nothing happens */
    reset_counts();
    s_profile = 0;
    press(1500);
    CHECK(s_recentreCalls == 0 && s_buzzes[0] == 0, "no Steam Frame profile: no system layer");
    s_profile = 1;

    /* the runtime moved the reference space: recentre without a buzz */
    reset_counts();
    vrSystemRuntimeRecentre();
    CHECK(s_recentreCalls == 1 && s_screenRecentres == 1 && s_buzzes[0] == 0, "a system recentre follows without a buzz");

    /* a LOCAL fallback space moves its origin with the recentre, so the standing height is taken again */
    reset_counts();
    s_floor = 0;
    vrSystemRuntimeRecentre();
    CHECK(s_lastFlags == 2, "a runtime recentre in a LOCAL space re-takes the standing height");
    s_floor = 1;

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("vr_system: all checks passed\n");
    return 0;
}
