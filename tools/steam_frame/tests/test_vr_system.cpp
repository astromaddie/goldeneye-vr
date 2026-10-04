/*
 * Host test of the Steam Frame system layer (port/vr/vr_system.cpp, the production file) with
 * sfvr's real state machine: a fake controller, haptics, game and clock. Run: tools/steam_frame/tests/run_tests.sh
 *
 * Covers standard.md section 1: View short press = back (and only that), hold 1 s = recentre + buzz on
 * both hands, hold 3 s = recentre + standing height + buzz again, one event per press, a press cut off by
 * focus loss or by another profile does nothing, and a button already down when the session starts is ignored.
 *
 * And the Menu + View chord (vr_system.h): VR settings once, after both are held together 0.5 s, with a
 * ui_click on both hands; the View press in it is no back and no recentre, and Menu reports itself part of
 * a chord (input.c then sends no START and starts no Menu hold) until both are up and a moment after.
 * These run on a synthetic clock through vrSystemStep, the function vrSystemFrame calls.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <openxr/openxr.h>

#include "../../../port/vr/vr_system.h"

static int s_profile = 1, s_view = 0, s_menu = 0, s_floor = 1;
static int s_settingsOpens, s_settingsOk = 1;
static int s_recentreCalls, s_lastFlags, s_screenRecentres, s_buzzes[2];
static double s_buzzAmp[2], s_buzzSec[2];
static double s_fakeNow;

extern "C" {
int gevrVrFrameProfile(void) { return s_profile; }
bool get_button_state(int hand, const char *name)
{
    return hand == 0 && ((!strcmp(name, "view") && s_view) || (!strcmp(name, "menu") && s_menu));
}
int gevrVrSettingsOpen(const char *) { s_settingsOpens++; return s_settingsOk; }
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
    s_settingsOpens = 0;
}

/* The chord on a synthetic clock: run vrSystemStep every 10 ms from t0 to t1 with the levels given
 * by `view(t)` and `menu(t)`; returns the time of the first VR_SYSTEM_SETTINGS event (or -1). */
static double s_t;
template <class V, class M>
static double run(double t1, V view, M menu)
{
    double fired = -1.0;
    for (; s_t <= t1 + 1e-9; s_t += 0.010) {
        const unsigned ev = vrSystemStep(view(s_t) ? 1 : 0, menu(s_t) ? 1 : 0, s_t);
        if ((ev & VR_SYSTEM_SETTINGS) && fired < 0.0) fired = s_t;
    }
    return fired;
}

static void chordTests()
{
    const double T = 1.0e6;
    s_t = T - 0.1;
    run(T - 0.05, [](double) { return false; }, [](double) { return false; });

    /* View first, Menu 0.1 s later, both held to 1.3 s: VR settings at 0.6 s, once; View held past 1 s
     * is no recentre; no back on release; Menu is part of a chord until a moment after both are up */
    reset_counts();
    double fired = run(T + 1.3, [T](double t) { return t >= T; }, [T](double t) { return t >= T + 0.1; });
    CHECK(fired >= T + 0.6 - 1e-6 && fired < T + 0.62, "Menu + View opens VR settings after 0.5 s held together");
    CHECK(s_settingsOpens == 1, "Menu + View opens VR settings once per press");
    CHECK(s_buzzes[0] == 1 && s_buzzes[1] == 1 && s_buzzAmp[1] > 0.49f && s_buzzAmp[1] < 0.51f
          && s_buzzSec[1] > 0.029f && s_buzzSec[1] < 0.031f, "the chord buzzes ui_click (0.5 / 30 ms) on both hands");
    CHECK(vrSystemMenuChordAt(s_t) == 1, "Menu is part of the chord while held");
    const double up = s_t;
    run(up + 0.05, [](double) { return false; }, [](double) { return false; });
    CHECK(s_recentreCalls == 0, "the chord's View press, held past 1 s, does not recentre");
    CHECK(vrSystemBackAt(s_t) == 0, "the chord's View press is no back");
    CHECK(vrSystemMenuChordAt(up + 0.2) == 1, "Menu still counts as the chord's just after both are up (no START)");
    CHECK(vrSystemMenuChordAt(up + 0.3) == 0, "and not 0.3 s later");

    /* Menu first, View 0.4 s later: 0.5 s from when both are down */
    reset_counts();
    double t0 = s_t + 0.5;
    run(t0 - 0.01, [](double) { return false; }, [](double) { return false; });
    fired = run(t0 + 1.0, [t0](double t) { return t >= t0 + 0.4; }, [t0](double t) { return t >= t0; });
    CHECK(fired >= t0 + 0.9 - 1e-6 && fired < t0 + 0.92 && s_settingsOpens == 1, "Menu then View: 0.5 s after both are down");
    run(s_t + 0.4, [](double) { return false; }, [](double) { return false; });

    /* let go after 0.3 s: nothing opens, and neither press does its own job (no back, no START) */
    reset_counts();
    t0 = s_t + 0.1;
    run(t0 - 0.01, [](double) { return false; }, [](double) { return false; });
    run(t0 + 0.3, [](double) { return true; }, [](double) { return true; });
    CHECK(vrSystemMenuChordAt(s_t) == 1, "an aborted chord still holds back Menu's START");
    run(s_t + 0.05, [](double) { return false; }, [](double) { return false; });
    CHECK(s_settingsOpens == 0 && s_buzzes[0] == 0, "both held only 0.3 s: no VR settings");
    CHECK(vrSystemBackAt(s_t) == 0 && s_recentreCalls == 0, "an aborted chord's View press is no back either");
    run(s_t + 0.4, [](double) { return false; }, [](double) { return false; });

    /* Menu pressed briefly during a long View hold: that View press is the chord's, so no recentre at
     * 1 s nor height at 3 s, and no back on release */
    reset_counts();
    t0 = s_t + 0.1;
    run(t0 - 0.01, [](double) { return false; }, [](double) { return false; });
    run(t0 + 3.2, [](double) { return true; }, [t0](double t) { return t >= t0 + 0.1 && t < t0 + 0.2; });
    run(s_t + 0.05, [](double) { return false; }, [](double) { return false; });
    CHECK(s_recentreCalls == 0 && vrSystemBackAt(s_t) == 0 && s_settingsOpens == 0,
          "a View press that took part in a chord neither recentres nor goes back");
    run(s_t + 0.4, [](double) { return false; }, [](double) { return false; });

    /* a gap restarts the 0.5 s: both 0.3 s, Menu up 0.1 s (View held), both again: opens 0.5 s into the second */
    reset_counts();
    t0 = s_t + 0.1;
    run(t0 - 0.01, [](double) { return false; }, [](double) { return false; });
    fired = run(t0 + 1.2, [](double) { return true; }, [t0](double t) { return t < t0 + 0.3 || t >= t0 + 0.4; });
    CHECK(fired >= t0 + 0.9 - 1e-6 && fired < t0 + 0.92 && s_settingsOpens == 1, "the 0.5 s are of both held without a gap");
    run(s_t + 0.4, [](double) { return false; }, [](double) { return false; });

    /* outside a game (the launcher): the chord is spent, nothing buzzes */
    reset_counts();
    s_settingsOk = 0;
    t0 = s_t + 0.1;
    run(t0 + 0.7, [t0](double t) { return t >= t0; }, [t0](double t) { return t >= t0; });
    CHECK(s_settingsOpens == 1 && s_buzzes[0] == 0 && s_buzzes[1] == 0, "no VR settings to open: no buzz");
    s_settingsOk = 1;
    run(s_t + 0.4, [](double) { return false; }, [](double) { return false; });

    /* afterwards View alone works as before: a short press is back, a 1.2 s hold recentres */
    reset_counts();
    t0 = s_t + 0.1;
    run(t0 + 0.12, [t0](double t) { return t >= t0; }, [](double) { return false; });
    run(s_t + 0.01, [](double) { return false; }, [](double) { return false; });
    CHECK(vrSystemBackAt(s_t) == 1 && s_recentreCalls == 0, "after a chord, a short View press is back again");
    run(s_t + 0.3, [](double) { return false; }, [](double) { return false; });
    t0 = s_t + 0.1;
    run(t0 + 1.2, [t0](double t) { return t >= t0; }, [](double) { return false; });
    run(s_t + 0.05, [](double) { return false; }, [](double) { return false; });
    CHECK(s_recentreCalls == 1 && vrSystemMenuChordAt(s_t) == 0, "after a chord, a View hold recentres again");
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

    chordTests();

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("vr_system: all checks passed\n");
    return 0;
}
