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
}

static sfvr_view_button s_view;
static double s_backUntilMs;
static unsigned long s_events;

static double nowS()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1.0e9;
}

unsigned vrSystemViewStep(int pressed, double now_s)
{
    return sfvr_view_update(&s_view, nullptr, pressed, now_s);
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

extern "C" void vrSystemFrame(int focused)
{
    if (!gevrVrFrameProfile() || !focused) {
        sfvr_view_reset(&s_view);   /* focus lost or another profile: cancel without events */
        return;
    }
    const unsigned ev = vrSystemViewStep(get_button_state(0, "view") ? 1 : 0, nowS());
    s_events += ev != 0;
    if (ev & SFVR_VIEW_SHORT_PRESS) {
        s_backUntilMs = nowS() * 1000.0 + 150.0;
        SLOG("[vr-sys] View short press -> back");
    }
    if (ev & SFVR_VIEW_RECENTRE) {
        recentre(1, true, "View held 1 s");
    }
    if (ev & SFVR_VIEW_RECALIBRATE_HEIGHT) {
        recentre(2, true, "View held 3 s");
    }
}

extern "C" void vrSystemRuntimeRecentre(void)
{
    recentre(1, false, "the runtime moved the reference space");
}

extern "C" int gevrSystemBack(void)
{
    return nowS() * 1000.0 < s_backUntilMs ? 1 : 0;
}

#endif /* GEVR_STEAM_FRAME */
