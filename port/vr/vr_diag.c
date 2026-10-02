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
    }
    /* the marker file the first bring-up used keeps working */
    if (access("gevr_force_render", F_OK) == 0) {
        s_diag.force_render = 1;
    }
    DIAG_LOG("[vr-diag] vr-diag.ini %s: force_render=%d diag_yaw=%.1f dump_frame=%d dump_stereo_frame=%d "
             "boot_stage=%d difficulty=%d timing=%d autostart=%d",
             s_diag.present ? "read" : "absent", s_diag.force_render, s_diag.diag_yaw, s_diag.dump_frame,
             s_diag.dump_stereo_frame, s_diag.boot_stage, s_diag.difficulty, s_diag.timing, s_diag.autostart);
}

const GevrDiag *gevrDiag(void)
{
    if (!s_loaded) load();
    return &s_diag;
}

#else /* not the Steam Frame build: no diagnostics */

static const GevrDiag s_none = { 0, 0.0f, 0, 0, 0, -1, 0, 0, 0 };
const GevrDiag *gevrDiag(void) { return &s_none; }

#endif

int gevrDiagBootStage(void) { return gevrDiag()->boot_stage; }
int gevrDiagDifficulty(void) { return gevrDiag()->difficulty; }
