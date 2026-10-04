/* See vr_system.h. Steam Frame only. */
#ifdef GEVR_STEAM_FRAME

#include "vr_system.h"

#include <ctime>
#include <android/log.h>
#include <openxr/openxr.h>

#include "sfvr/sfvr_view.h"

#define SLOG(...) __android_log_print(ANDROID_LOG_INFO, "GoldenEye-VR", __VA_ARGS__)

extern "C" {
int gevrVrFrameProfile(void);                                  /* vr_input.cpp */
bool get_button_state(int hand_index, const char *button_name); /* vr_input.cpp */
XrResult trigger_haptic_vibration_c(int hand_index, float amplitude, float duration);
void vr_screen_recenter(void);                                 /* vr_openxr.cpp */
void gevrRequestRecenter(int flags);                           /* port/src/input.c */
int gevrVrFloorRelative(void);                                 /* vr_openxr.cpp */
int gevrVrSettingsOpen(const char *why);                       /* vr_launcher.cpp: 0 = not in a game */
}

static sfvr_view_button s_view;
static double s_backUntilS;
static unsigned long s_events;

/* Menu + View (vr_system.h) */
static bool s_chordLatched;   // both have been down together in this press; until both are up
static bool s_chordBothWas;
static bool s_chordFired;
static double s_chordStartS;
static double s_chordEndS = -1.0e9;

static double nowS()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1.0e9;
}

static void buzzBoth()
{
    /* standard.md section 6, `system`: 0.6 for 80 ms, both hands */
    trigger_haptic_vibration_c(0, 0.6f, 0.08f);
    trigger_haptic_vibration_c(1, 0.6f, 0.08f);
}

static void recentre(int flags, bool buzz, const char *why)
{
    gevrRequestRecenter(flags);   /* the game takes it at its next stereo frame boundary */
    vr_screen_recenter();         /* the world-locked panels (menus, cutscenes, the launcher) */
    if (buzz) buzzBoth();
    SLOG("[vr-sys] recentre%s (%s)", flags >= 2 ? " + standing height" : "", why);
}

static void chordReset()
{
    s_chordLatched = s_chordBothWas = s_chordFired = false;
}

unsigned vrSystemStep(int view, int menu, double now_s)
{
    unsigned ev = 0;
    view = view != 0;
    menu = menu != 0;

    /* Menu + View: latched while both take part, so neither press does its own job */
    const bool both = view && menu;
    if (both && !s_chordLatched) {
        s_chordLatched = true;
        s_chordFired = false;
        SLOG("[vr-sys] Menu + View: held together");
    }
    if (both && !s_chordBothWas) {
        s_chordStartS = now_s;   /* 0.5 s of both held together, without a gap */
    }
    s_chordBothWas = both;
    if (both && !s_chordFired && now_s - s_chordStartS >= VR_SYSTEM_CHORD_HOLD_S) {
        s_chordFired = true;
        ev |= VR_SYSTEM_SETTINGS;
    }
    if (s_chordLatched) {
        /* this View press is the chord's: no back, no recentre; one still down after it is ignored
         * until it is let go (sfvr_view_reset's rule for a press it did not see start) */
        sfvr_view_reset(&s_view);
        sfvr_view_update(&s_view, nullptr, view, now_s);
        if (!view && !menu) {
            s_chordLatched = false;
            s_chordEndS = now_s;
        }
    } else {
        ev |= sfvr_view_update(&s_view, nullptr, view, now_s);
    }

    s_events += ev != 0;
    if (ev & SFVR_VIEW_SHORT_PRESS) {
        s_backUntilS = now_s + 0.150;
        SLOG("[vr-sys] View short press -> back");
    }
    if (ev & SFVR_VIEW_RECENTRE) {
        recentre(1, true, "View held 1 s");
    }
    if (ev & SFVR_VIEW_RECALIBRATE_HEIGHT) {
        recentre(2, true, "View held 3 s");
    }
    if (ev & VR_SYSTEM_SETTINGS) {
        if (gevrVrSettingsOpen("Menu + View")) {
            /* standard.md section 6, `ui_click`: 0.5 for 30 ms (both hands: both are on the chord) */
            trigger_haptic_vibration_c(0, 0.5f, 0.03f);
            trigger_haptic_vibration_c(1, 0.5f, 0.03f);
            SLOG("[vr-sys] Menu + View held 0.5 s -> VR settings");
        } else {
            SLOG("[vr-sys] Menu + View held 0.5 s: no VR settings here (the launcher has them)");
        }
    }
    return ev;
}

unsigned vrSystemViewStep(int pressed, double now_s)
{
    return vrSystemStep(pressed, 0, now_s);
}

int vrSystemBackAt(double now_s)
{
    return now_s < s_backUntilS ? 1 : 0;
}

int vrSystemMenuChordAt(double now_s)
{
    /* a moment after both are up: input.c reads Menu once a game frame, after this has seen it */
    return s_chordLatched || now_s - s_chordEndS < 0.25 ? 1 : 0;
}

extern "C" void vrSystemFrame(int focused)
{
    if (!gevrVrFrameProfile() || !focused) {
        sfvr_view_reset(&s_view);   /* focus lost or another profile: cancel without events */
        if (s_chordLatched) s_chordEndS = nowS();
        chordReset();
        return;
    }
    vrSystemStep(get_button_state(0, "view") ? 1 : 0, get_button_state(0, "menu") ? 1 : 0, nowS());
}

extern "C" void vrSystemRuntimeRecentre(void)
{
    /* A LOCAL fallback space moves its origin to the head's height, so the old standing height is
     * meaningless after the runtime recentres it: take it again there. A floor space keeps it. */
    recentre(gevrVrFloorRelative() ? 1 : 2, false, "the runtime moved the reference space");
}

extern "C" int gevrSystemBack(void)
{
    return vrSystemBackAt(nowS());
}

extern "C" int gevrSystemMenuChord(void)
{
    return vrSystemMenuChordAt(nowS());
}

#endif /* GEVR_STEAM_FRAME */
