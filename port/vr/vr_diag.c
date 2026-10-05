/* See vr_diag.h. */
#include "vr_diag.h"

#ifdef GEVR_STEAM_FRAME

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef ANDROID
#include <android/log.h>
#define DIAG_LOG(...) __android_log_print(ANDROID_LOG_INFO, "GoldenEye-VR", __VA_ARGS__)
#else
#define DIAG_LOG(...) (printf(__VA_ARGS__), printf("\n"))
#endif

static GevrDiag s_diag = { 0, 0.0f, 0, 0, 0, -1, 0, 0, 0 };
static int s_loaded;

static int parse_bool(const char *v)
{
    return v[0] == '1' || v[0] == 't' || v[0] == 'T' || v[0] == 'y' || v[0] == 'Y' || strncmp(v, "on", 2) == 0;
}

static void apply(const char *key, const char *val)
{
    if (!strcmp(key, "force_render")) s_diag.force_render = parse_bool(val);
    else if (!strcmp(key, "diag_yaw")) s_diag.diag_yaw = (float)atof(val);
    else if (!strcmp(key, "dump_frame")) s_diag.dump_frame = atoi(val);
    else if (!strcmp(key, "dump_stereo_frame")) s_diag.dump_stereo_frame = atoi(val);
    else if (!strcmp(key, "boot_stage")) s_diag.boot_stage = (int)strtol(val, NULL, 0);
    else if (!strcmp(key, "difficulty")) s_diag.difficulty = atoi(val);
    else if (!strcmp(key, "timing") || !strcmp(key, "timing_gpu")) s_diag.timing = parse_bool(val);
    else if (!strcmp(key, "autostart")) s_diag.autostart = parse_bool(val);
    else if (!strcmp(key, "exit_after_s")) s_diag.exit_after_s = (float)atof(val);
    else if (!strcmp(key, "one_shot")) s_diag.one_shot = parse_bool(val);
    else if (!strcmp(key, "refresh_rate")) s_diag.refresh_rate = atoi(val);
    else if (!strcmp(key, "diag_walk")) s_diag.diag_walk = atoi(val) < 0 ? -1 : atoi(val) > 0 ? 1 : 0;
    else if (!strcmp(key, "diag_turn")) {
        float t = (float)atof(val);
        s_diag.diag_turn = t < -100.0f ? -100.0f : t > 100.0f ? 100.0f : t;
    }
    else if (!strcmp(key, "dump_watch_frame")) s_diag.dump_watch_frame = atoi(val);
    else if (!strcmp(key, "watch_backdrop")) s_diag.watch_backdrop = parse_bool(val) ? 1 : -1;
    else if (!strcmp(key, "dump_scope")) s_diag.dump_scope = atoi(val);
    else if (!strcmp(key, "diag_gl")) s_diag.diag_gl = parse_bool(val);
    else if (!strcmp(key, "dump_hud")) s_diag.dump_hud = atoi(val);
    else if (!strcmp(key, "diag_aim")) s_diag.diag_aim = (float)atof(val);
    else if (!strcmp(key, "diag_aim_eye")) s_diag.diag_aim_eye = atoi(val) != 0 ? 1 : 0;
    else if (!strcmp(key, "diag_aim_yaw")) s_diag.diag_aim_yaw = (float)atof(val);
    else if (!strcmp(key, "diag_aim_pitch")) s_diag.diag_aim_pitch = (float)atof(val);
    else if (!strcmp(key, "diag_aim_roll")) s_diag.diag_aim_roll = (float)atof(val);
    else if (!strcmp(key, "diag_aim_dx")) s_diag.diag_aim_dx = (float)atof(val);
    else if (!strcmp(key, "diag_aim_swing")) s_diag.diag_aim_swing = (float)atof(val);
    else if (!strcmp(key, "diag_roll")) s_diag.diag_roll = (float)atof(val);
    else if (!strcmp(key, "diag_point")) s_diag.diag_point = parse_bool(val);
    else if (!strcmp(key, "dump_cheat_frame")) s_diag.dump_cheat_frame = atoi(val);
    else if (!strcmp(key, "render_scale")) s_diag.render_scale = (float)atof(val);
    else if (!strcmp(key, "msaa")) s_diag.msaa = atoi(val);
    else if (!strcmp(key, "dump_cinema_frame")) s_diag.dump_cinema_frame = atoi(val);
    else if (!strcmp(key, "cinema_sep_mm")) s_diag.cinema_sep_mm = (float)atof(val);
    else DIAG_LOG("[vr-diag] unknown key '%s'", key);
}

static void load(void)
{
    FILE *f = fopen("vr-diag.ini", "r");
    char line[256];

    s_loaded = 1;
    if (f) {
        s_diag.present = 1;
        while (fgets(line, sizeof(line), f)) {
            char *p = line, *eq, *key, *val, *end;

            while (*p == ' ' || *p == '\t') p++;
            if (*p == '#' || *p == ';' || *p == '[' || *p == '\n' || *p == '\r' || *p == 0) continue;
            eq = strchr(p, '=');
            if (!eq) continue;
            *eq = 0;
            key = p;
            val = eq + 1;
            end = key + strlen(key);
            while (end > key && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
            while (*val == ' ' || *val == '\t') val++;
            end = val + strlen(val);
            while (end > val && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) *--end = 0;
            apply(key, val);
        }
        fclose(f);
        /* a one-shot run (a Steam launch armed from the Mac) must not leak into the next normal launch */
        if (s_diag.one_shot && rename("vr-diag.ini", "vr-diag.ini.used") != 0) {
            DIAG_LOG("[vr-diag] one_shot: could not rename vr-diag.ini");
        }
    }
    /* the marker file the first bring-up used keeps working */
    if (access("gevr_force_render", F_OK) == 0) {
        s_diag.force_render = 1;
    }
    DIAG_LOG("[vr-diag] vr-diag.ini %s: force_render=%d diag_yaw=%.1f dump_frame=%d dump_stereo_frame=%d "
             "boot_stage=%d difficulty=%d timing=%d autostart=%d exit_after_s=%.0f one_shot=%d refresh_rate=%d "
             "diag_walk=%d diag_turn=%.0f dump_watch_frame=%d watch_backdrop=%d dump_scope=%d diag_gl=%d dump_hud=%d "
             "diag_aim=%.0f diag_aim_eye=%d diag_roll=%.0f diag_point=%d dump_cheat_frame=%d render_scale=%.2f msaa=%d "
             "dump_cinema_frame=%d cinema_sep_mm=%.0f",
             s_diag.present ? "read" : "absent", s_diag.force_render, s_diag.diag_yaw, s_diag.dump_frame,
             s_diag.dump_stereo_frame, s_diag.boot_stage, s_diag.difficulty, s_diag.timing, s_diag.autostart,
             s_diag.exit_after_s, s_diag.one_shot, s_diag.refresh_rate, s_diag.diag_walk, s_diag.diag_turn,
             s_diag.dump_watch_frame, s_diag.watch_backdrop, s_diag.dump_scope, s_diag.diag_gl, s_diag.dump_hud,
             s_diag.diag_aim, s_diag.diag_aim_eye, s_diag.diag_roll, s_diag.diag_point, s_diag.dump_cheat_frame,
             s_diag.render_scale, s_diag.msaa,
             s_diag.dump_cinema_frame, s_diag.cinema_sep_mm);
}

const GevrDiag *gevrDiag(void)
{
    if (!s_loaded) load();
    return &s_diag;
}

void gevrQuitToSteam(void); /* vr_openxr.cpp */

void gevrDiagFrameTick(double nowMs)
{
    static double start;
    static int quit;
    const GevrDiag *d = gevrDiag();

    if (d->exit_after_s <= 0.0f || quit) return;
    if (start == 0.0) start = nowMs;
    if (nowMs - start >= d->exit_after_s * 1000.0) {
        quit = 1;
        DIAG_LOG("[vr-diag] exit_after_s=%.0f reached: quitting to Steam", d->exit_after_s);
        gevrQuitToSteam();
    }
}

#else /* not the Steam Frame build: no diagnostics */

static const GevrDiag s_none = { 0, 0.0f, 0, 0, 0, -1, 0, 0, 0 };
const GevrDiag *gevrDiag(void) { return &s_none; }
void gevrDiagFrameTick(double nowMs) { (void)nowMs; }

#endif

int gevrDiagBootStage(void) { return gevrDiag()->boot_stage; }
float gevrDiagTurn(void) { return gevrDiag()->diag_turn / 100.0f; }
int gevrDiagDifficulty(void) { return gevrDiag()->difficulty; }
