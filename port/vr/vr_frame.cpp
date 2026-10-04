/* See vr_frame.h. Everything here is Steam Frame only. */
#ifdef GEVR_STEAM_FRAME

#include "vr_frame.h"
#include "vr_diag.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>
#include <sys/stat.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>

#include "sfvr/sfvr_perf.h"

#define FLOG(...) __android_log_print(ANDROID_LOG_INFO, "GoldenEye-VR", __VA_ARGS__)

extern "C" int bossGetStageNum(void);

double vrFrameNowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

/* ---------------------------------------------------------------- synthetic head */

extern "C" int vrFrameForceRender(void) { return gevrDiag()->force_render; }

extern "C" void vrFrameSynthHead(float pos[3], float quat[4])
{
    const float yaw = gevrDiag()->diag_yaw * 3.14159265f / 180.0f;
    pos[0] = 0.0f;
    pos[1] = 1.6f;
    pos[2] = 0.0f;
    quat[0] = 0.0f;
    quat[1] = sinf(yaw * 0.5f);
    quat[2] = 0.0f;
    quat[3] = cosf(yaw * 0.5f);
}

/* ---------------------------------------------------------------------- dumps */

static const char *kDumpDir = "vr-dumps";
static bool s_dumpActive;            /* the eye image of this XR frame is to be dumped */
static bool s_dumpStereoPending;     /* a stereo gameplay frame is the dump target */
static unsigned s_stereoFrames;
static bool s_dumpDone;          /* both eye layers were really written: the dump is complete */
static bool s_dumpWrote;
static bool s_dumpEyesOk;        /* this frame's eye layers were written */
static int s_dumpTries;          /* frames that were armed but wrote nothing (no eye pass, GL error) */
static char s_stateLine[640];
static bool s_stateWritten;
static uint64_t s_xrFrame;           /* the XR frame being drawn (vrFrameDumpFrameBegin) */
static uint64_t s_watchDumpAt;       /* dump_watch_frame: the XR frame to dump at, 0 = not armed */
static bool s_dumpNeedScreen;        /* this dump is complete only with the virtual screen too */
static bool s_dumpScreenOk;

static bool write_bmp24(const char *path, const uint8_t *rgba, int w, int h, unsigned *opaquePct)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        FLOG("[vr-dump] cannot write %s", path);
        return false;
    }
    const int rowBytes = (w * 3 + 3) & ~3;
    const uint32_t imageSize = (uint32_t)rowBytes * (uint32_t)h;
    const uint32_t fileSize = 54 + imageSize;
    uint8_t hdr[54] = { 'B', 'M' };
    memcpy(hdr + 2, &fileSize, 4);
    const uint32_t off = 54, dib = 40, planes = 1, bpp = 24;
    memcpy(hdr + 10, &off, 4);
    memcpy(hdr + 14, &dib, 4);
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);   /* positive: rows bottom-up, which is GL's read order */
    memcpy(hdr + 26, &planes, 2);
    memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &imageSize, 4);
    fwrite(hdr, 1, sizeof(hdr), f);
    std::vector<uint8_t> row((size_t)rowBytes, 0);
    unsigned long opaque = 0;
    for (int y = 0; y < h; y++) {
        const uint8_t *src = rgba + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            row[(size_t)x * 3 + 0] = src[x * 4 + 2];
            row[(size_t)x * 3 + 1] = src[x * 4 + 1];
            row[(size_t)x * 3 + 2] = src[x * 4 + 0];
            if (src[x * 4 + 3] >= 250) opaque++;
        }
        fwrite(row.data(), 1, (size_t)rowBytes, f);
    }
    fclose(f);
    if (opaquePct) *opaquePct = (unsigned)(opaque * 100ull / ((unsigned long)w * (unsigned long)h));
    return true;
}

/* Reads GL_READ_FRAMEBUFFER's colour attachment 0 and writes it. */
static bool dump_read_framebuffer(const char *name, int w, int h, unsigned *opaquePct)
{
    std::vector<uint8_t> px((size_t)w * h * 4);
    GLint prevRead = 0, prevAlign = 4;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glGetIntegerv(GL_PACK_ALIGNMENT, &prevAlign);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    /* errors the renderer left pending must not be mistaken for this read's */
    for (int n = 0; n < 16 && glGetError() != GL_NO_ERROR; n++) {}
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    const GLenum err = glGetError();
    glPixelStorei(GL_PACK_ALIGNMENT, prevAlign);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    if (err != GL_NO_ERROR) {
        FLOG("[vr-dump] glReadPixels failed 0x%x for %s", err, name);
        return false;
    }
    mkdir(kDumpDir, 0777);
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", kDumpDir, name);
    return write_bmp24(path, px.data(), w, h, opaquePct);
}

/* ------------------------------------------------------------ dump_scope */

static unsigned s_scopeFrames;
static int s_scopeDumpStage;     /* 0 waiting, 1 target written (lens next), 2 lens written, 3 noted: done */
static int s_scopeDumpHand = -1;

/* Reads colour attachment 0 of a texture (one layer of an array, or a 2D texture) and writes it;
 * `litPct` is the share of pixels brighter than black (any channel above 8). */
static bool dump_texture(const char *name, unsigned tex, int layer, int w, int h, unsigned *litPct, GLenum *errOut)
{
    static GLuint fbo = 0;
    GLint prevRead = 0, prevAlign = 4;
    std::vector<uint8_t> px((size_t)w * h * 4);
    if (!fbo) glGenFramebuffers(1, &fbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glGetIntegerv(GL_PACK_ALIGNMENT, &prevAlign);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    if (layer >= 0) {
        glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 0, layer);
    } else {
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    }
    const GLenum st = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    GLenum err = GL_NO_ERROR;
    if (st == GL_FRAMEBUFFER_COMPLETE) {
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        for (int n = 0; n < 16 && glGetError() != GL_NO_ERROR; n++) {}
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        err = glGetError();
        glPixelStorei(GL_PACK_ALIGNMENT, prevAlign);
    }
    if (layer >= 0) {
        glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0, 0, 0);
    } else {
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    if (errOut) *errOut = st != GL_FRAMEBUFFER_COMPLETE ? st : err;
    if (st != GL_FRAMEBUFFER_COMPLETE || err != GL_NO_ERROR) {
        FLOG("[vr-dump] %s: read framebuffer 0x%x, glReadPixels 0x%x", name, st, err);
        return false;
    }
    unsigned long lit = 0;
    for (size_t i = 0; i < px.size(); i += 4) {
        if (px[i] > 8 || px[i + 1] > 8 || px[i + 2] > 8) lit++;
    }
    if (litPct) *litPct = (unsigned)(lit * 100ull / ((unsigned long)w * (unsigned long)h));
    mkdir(kDumpDir, 0777);
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", kDumpDir, name);
    return write_bmp24(path, px.data(), w, h, nullptr);
}

static void scope_txt(const char *mode, const char *line)
{
    char path[128];
    mkdir(kDumpDir, 0777);
    snprintf(path, sizeof(path), "%s/vr-scope.txt", kDumpDir);
    FILE *f = fopen(path, mode);
    if (f) {
        fprintf(f, "%s\n", line);
        fclose(f);
    }
    FLOG("[vr-dump] scope: %s", line);
}

extern "C" int vrFrameScopeDumpDue(void)
{
    const GevrDiag *d = gevrDiag();
    if (d->dump_scope <= 0 || s_scopeDumpStage != 0) return 0;
    return ++s_scopeFrames >= (unsigned)d->dump_scope ? 1 : 0;
}

extern "C" void vrFrameScopeDumpTarget(int hand, unsigned tex, int layer, int w, int h, const char *state)
{
    unsigned lit = 0;
    GLenum err = GL_NO_ERROR;
    const bool ok = dump_texture("vr-scope.bmp", tex, layer, w, h, &lit, &err);
    char line[1024];
    snprintf(line, sizeof(line), "%s\ntarget_dump=%s read_error=0x%x lit_pct=%u (scope pass %u)", state,
             ok ? "vr-scope.bmp" : "failed", err, lit, s_scopeFrames);
    scope_txt("w", line);
    s_scopeDumpStage = 1;
    s_scopeDumpHand = hand;
    /* with diag_aim the eyes too, this frame (the scope pass comes before the eye pass ends):
     * the rifle's eyepiece as drawn, to hold the lens layer (vr-scope.txt) against */
    if (gevrDiag()->diag_aim > 0.0f && !s_dumpDone) {
        s_dumpActive = true;
    }
}

extern "C" int vrFrameScopeLensDue(int hand)
{
    return s_scopeDumpStage == 1 && hand == s_scopeDumpHand ? 1 : 0;
}

extern "C" void vrFrameScopeDumpLens(int hand, unsigned image, int w, int h, const char *state)
{
    unsigned lit = 0;
    GLenum err = GL_NO_ERROR;
    if (s_scopeDumpStage != 1 || hand != s_scopeDumpHand) return;
    const bool ok = dump_texture("vr-scope-lens.bmp", image, -1, w, h, &lit, &err);
    char line[768];
    snprintf(line, sizeof(line), "%s lens_dump=%s read_error=0x%x lens_lit_pct=%u", state,
             ok ? "vr-scope-lens.bmp" : "failed", err, lit);
    scope_txt("a", line);
    s_scopeDumpStage = 2;
}

extern "C" void vrFrameScopeNote(int hand, const char *line)
{
    if (s_scopeDumpStage != 2 || hand != s_scopeDumpHand) return;
    scope_txt("a", line);
    s_scopeDumpStage = 3;
    FLOG("[vr-dump] scope dump complete: vr-dumps/vr-scope.bmp, vr-scope-lens.bmp, vr-scope.txt");
}

/* ------------------------------------------------------------ dump_hud */

extern "C" void vrFrameHudDump(const char *name, unsigned tex, int layer, int w, int h)
{
    static unsigned frames;
    static uint64_t lastFrame = ~0ull, dumpFrame = ~0ull;
    const GevrDiag *d = gevrDiag();
    if (d->dump_hud <= 0 || tex == 0) return;
    if (s_xrFrame != lastFrame) {
        lastFrame = s_xrFrame;
        if (++frames == (unsigned)d->dump_hud) dumpFrame = s_xrFrame;
    }
    if (s_xrFrame != dumpFrame) return;
    unsigned lit = 0;
    GLenum err = GL_NO_ERROR;
    const bool ok = dump_texture(name, tex, layer, w, h, &lit, &err);
    FLOG("[vr-dump] hud: %s %dx%d %s, lit_pct=%u (layer %d, XR frame %llu, stage %d)", name, w, h,
         ok ? "written" : "failed", lit, layer, (unsigned long long)s_xrFrame, bossGetStageNum());
}

extern "C" void vrFrameDumpFrameBegin(uint64_t xrFrame)
{
    const GevrDiag *d = gevrDiag();
    s_dumpActive = false;
    s_dumpEyesOk = false;
    s_dumpWrote = false;
    s_stateLine[0] = 0;
    s_dumpScreenOk = false;
    s_xrFrame = xrFrame;
    if (s_dumpDone || s_dumpTries > 600) return;
    /* dump_watch_frame: N XR frames after the paused watch's screen was placed, eyes and screen */
    if (s_watchDumpAt != 0 && xrFrame >= s_watchDumpAt) {
        s_dumpActive = true;
        s_dumpNeedScreen = true;
    }
    /* from frame N on, until a frame really draws the eyes: frame N itself may draw none
     * (a redraw with nothing to replay, a mode switch) */
    if (d->dump_frame > 0 && xrFrame >= (uint64_t)d->dump_frame) {
        s_dumpActive = true;
    }
}

extern "C" void vrFrameDumpStereoFrame(void)
{
    const GevrDiag *d = gevrDiag();
    s_stereoFrames++;
    if (!s_dumpDone && d->dump_stereo_frame > 0 && s_stereoFrames >= (unsigned)d->dump_stereo_frame) {
        s_dumpActive = true;
        s_dumpStereoPending = true;
    }
}

extern "C" int vrFrameDumpDue(void) { return s_dumpActive ? 1 : 0; }

extern "C" void vrFrameDumpWatchPlaced(void)
{
    const GevrDiag *d = gevrDiag();
    if (d->dump_watch_frame > 0 && !s_dumpDone && s_watchDumpAt == 0) {
        s_watchDumpAt = s_xrFrame + (uint64_t)d->dump_watch_frame;
        FLOG("[vr-dump] armed for the paused watch: XR frame %llu (%d after the screen was placed)",
             (unsigned long long)s_watchDumpAt, d->dump_watch_frame);
    }
}

extern "C" void vrFrameDumpCinemaPlaced(void)
{
    const GevrDiag *d = gevrDiag();
    if (d->dump_cinema_frame > 0 && !s_dumpDone && s_watchDumpAt == 0) {
        s_watchDumpAt = s_xrFrame + (uint64_t)d->dump_cinema_frame;
        FLOG("[vr-dump] armed for the 3D cinema: XR frame %llu (%d after the screen was placed)",
             (unsigned long long)s_watchDumpAt, d->dump_cinema_frame);
    }
}

extern "C" void vrFrameDumpEyes(unsigned tex, int w, int h)
{
    static GLuint fbo = 0;
    GLint prevRead = 0;
    unsigned pct[2] = { 0, 0 };
    bool ok = true;

    if (!s_dumpActive) return;
    if (!fbo) glGenFramebuffers(1, &fbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    for (int layer = 0; layer < 2; layer++) {
        char name[32];
        snprintf(name, sizeof(name), "vr-eye%d.bmp", layer);
        glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 0, layer);
        const GLenum st = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
        if (st != GL_FRAMEBUFFER_COMPLETE) {
            FLOG("[vr-dump] read framebuffer for layer %d incomplete (0x%x)", layer, st);
            ok = false;
            continue;
        }
        /* dump_read_framebuffer restores the binding; set it again for the second layer */
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        ok = dump_read_framebuffer(name, w, h, &pct[layer]) && ok;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    }
    glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0, 0, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    s_dumpWrote = ok;
    s_dumpEyesOk = ok;
    snprintf(s_stateLine, sizeof(s_stateLine), "eye=%dx%d opaque_pct=%u/%u", w, h, pct[0], pct[1]);
}

extern "C" void vrFrameDumpScreen(int w, int h)
{
    unsigned pct = 0;
    if (!s_dumpActive) return;
    if (dump_read_framebuffer("vr-hud.bmp", w, h, &pct)) {
        s_dumpWrote = true;
        s_dumpScreenOk = true;
        char extra[64];
        snprintf(extra, sizeof(extra), " screen=%dx%d screen_opaque_pct=%u", w, h, pct);
        strncat(s_stateLine, extra, sizeof(s_stateLine) - strlen(s_stateLine) - 1);
    }
}

extern "C" void vrFrameDumpPanel(const char *name, int w, int h)
{
    unsigned pct = 0;
    if (!s_dumpActive) return;
    if (dump_read_framebuffer(name, w, h, &pct)) {
        s_dumpWrote = true;
        s_dumpScreenOk = true;
        char extra[96];
        snprintf(extra, sizeof(extra), " %s=%dx%d opaque_pct=%u", name, w, h, pct);
        strncat(s_stateLine, extra, sizeof(s_stateLine) - strlen(s_stateLine) - 1);
    }
}

extern "C" void vrFrameDumpState(const float headPos[3], const float headQuat[4], const float eyePos[2][3],
                                 const float eyeFov[2][4], uint64_t xrFrame, long long swapchainFormat)
{
    if (!s_dumpActive || s_stateWritten) return;
    s_stateWritten = true;
    char path[128];
    snprintf(path, sizeof(path), "%s/vr-state.txt", kDumpDir);
    mkdir(kDumpDir, 0777);
    FILE *f = fopen(path, "w");
    if (!f) return;
    const GevrDiag *d = gevrDiag();
    fprintf(f, "xr_frame=%llu stereo_frames=%u stage=%d diag_yaw=%.1f force_render=%d\n",
            (unsigned long long)xrFrame, s_stereoFrames, bossGetStageNum(), d->diag_yaw, d->force_render);
    fprintf(f, "head_pos=%.3f,%.3f,%.3f head_quat=%.4f,%.4f,%.4f,%.4f\n", headPos[0], headPos[1], headPos[2],
            headQuat[0], headQuat[1], headQuat[2], headQuat[3]);
    for (int e = 0; e < 2; e++) {
        fprintf(f, "eye%d_pos=%.4f,%.4f,%.4f fov_l_r_u_d=%.4f,%.4f,%.4f,%.4f\n", e, eyePos[e][0], eyePos[e][1],
                eyePos[e][2], eyeFov[e][0], eyeFov[e][1], eyeFov[e][2], eyeFov[e][3]);
    }
    fprintf(f, "swapchain_format=0x%llx (0x8c43 = GL_SRGB8_ALPHA8: glReadPixels may return linear values on some drivers; "
               "compare with the displayed image before trusting absolute levels)\n", swapchainFormat);
    fprintf(f, "ipd_m=%.4f\n", sqrtf((eyePos[1][0] - eyePos[0][0]) * (eyePos[1][0] - eyePos[0][0]) +
                                     (eyePos[1][1] - eyePos[0][1]) * (eyePos[1][1] - eyePos[0][1]) +
                                     (eyePos[1][2] - eyePos[0][2]) * (eyePos[1][2] - eyePos[0][2])));
    fclose(f);
}

extern "C" void vrFrameDumpFinish(void)
{
    if (!s_dumpActive) return;
    /* the state file gets the image stats appended */
    char path[128];
    snprintf(path, sizeof(path), "%s/vr-state.txt", kDumpDir);
    FILE *f = fopen(path, "a");
    if (f) {
        fprintf(f, "%s\n", s_stateLine);
        fclose(f);
    }
    s_stateWritten = false;
    if (s_dumpEyesOk && s_dumpNeedScreen && !s_dumpScreenOk) {
        /* the paused watch's dump wants both images from one game frame: an in-between XR
         * frame redraws the eyes only, so wait for the next game frame (files are rewritten) */
        if (++s_dumpTries == 1 || s_dumpTries % 120 == 0) {
            FLOG("[vr-dump] eyes written without the virtual screen (an in-between frame); trying again");
        }
    } else if (s_dumpEyesOk) {
        FLOG("[vr-dump] wrote %s/vr-eye0.bmp, vr-eye1.bmp (%s)", kDumpDir, s_stateLine);
        s_dumpDone = true;   /* one complete dump per run */
    } else {
        /* nothing was drawn into the eyes this frame (or the read failed): stay armed for the next one */
        if (++s_dumpTries == 1 || s_dumpTries % 120 == 0) {
            FLOG("[vr-dump] armed, but this frame wrote no eye images (%d so far)%s", s_dumpTries,
                 s_dumpWrote ? "; the virtual screen was written" : "");
        }
    }
    s_dumpActive = false;
    (void)s_dumpStereoPending;
}

/* ----------------------------------------------------------------- [vr-perf] */

static sfvr_perf s_perf;
static bool s_perfInit;
static double s_stageMs[3];            /* render, redraw, end: this XR frame so far */
static double s_prevFrameStartMs;
static bool s_redraw;
static float s_gpuMsThisFrame = -1.0f;
static double s_waitSumMs;
static unsigned long s_waitFrames;

/* GPU timer queries (GL_EXT_disjoint_timer_query), read a few frames late. */
#define GL_TIME_ELAPSED_EXT 0x88BF
#define GL_GPU_DISJOINT_EXT 0x8FBB
typedef void (*PFN_genq)(GLsizei, GLuint *);
typedef void (*PFN_delq)(GLsizei, const GLuint *);
typedef void (*PFN_beginq)(GLenum, GLuint);
typedef void (*PFN_endq)(GLenum);
typedef void (*PFN_getqui)(GLuint, GLenum, GLuint *);
typedef void (*PFN_getqui64)(GLuint, GLenum, unsigned long long *);
static PFN_genq pGenQ;
static PFN_beginq pBeginQ;
static PFN_endq pEndQ;
static PFN_getqui pGetQui;
static PFN_getqui64 pGetQui64;
static bool s_gpuTried, s_gpuOk;
static GLuint s_queries[8];
static bool s_queryPending[8];
static int s_queryHead;                /* next query to begin */
static bool s_queryOpen;

static void gpu_init(void)
{
    if (s_gpuTried) return;
    s_gpuTried = true;
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    if (!ext || !strstr(ext, "GL_EXT_disjoint_timer_query")) {
        FLOG("[vr-perf] GPU timer: GL_EXT_disjoint_timer_query not advertised, gpu=n/a");
        return;
    }
    pGenQ = (PFN_genq)eglGetProcAddress("glGenQueriesEXT");
    pBeginQ = (PFN_beginq)eglGetProcAddress("glBeginQueryEXT");
    pEndQ = (PFN_endq)eglGetProcAddress("glEndQueryEXT");
    pGetQui = (PFN_getqui)eglGetProcAddress("glGetQueryObjectuivEXT");
    pGetQui64 = (PFN_getqui64)eglGetProcAddress("glGetQueryObjectui64vEXT");
    if (!pGenQ || !pBeginQ || !pEndQ || !pGetQui || !pGetQui64) {
        FLOG("[vr-perf] GPU timer: entry points missing, gpu=n/a");
        return;
    }
    pGenQ(8, s_queries);
    s_gpuOk = true;
    FLOG("[vr-perf] GPU timer: GL_EXT_disjoint_timer_query ready");
}

extern "C" void vrFrameGpuBegin(void)
{
    if (!gevrDiag()->timing) return;
    gpu_init();
    if (!s_gpuOk || s_queryOpen || s_queryPending[s_queryHead]) return;
    pBeginQ(GL_TIME_ELAPSED_EXT, s_queries[s_queryHead]);
    s_queryOpen = true;
}

extern "C" void vrFrameGpuEnd(void)
{
    if (!s_gpuOk || !s_queryOpen) return;
    pEndQ(GL_TIME_ELAPSED_EXT);
    s_queryPending[s_queryHead] = true;
    s_queryHead = (s_queryHead + 1) & 7;
    s_queryOpen = false;
}

/* Collect the queries that have finished (never waits). */
static void gpu_collect(void)
{
    if (!s_gpuOk) return;
    GLint disjoint = 0;
    glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
    for (int i = 0; i < 8; i++) {
        if (!s_queryPending[i]) continue;
        GLuint avail = 0;
        pGetQui(s_queries[i], 0x8867 /* GL_QUERY_RESULT_AVAILABLE_EXT */, &avail);
        if (!avail) continue;
        unsigned long long ns = 0;
        pGetQui64(s_queries[i], 0x8866 /* GL_QUERY_RESULT_EXT */, &ns);
        s_queryPending[i] = false;
        if (!disjoint) s_gpuMsThisFrame = (float)(ns / 1.0e6);
    }
}

extern "C" void vrFramePerfSetRedraw(int redraw) { s_redraw = redraw != 0; }
extern "C" int vrFramePerfIsRedraw(void) { return s_redraw ? 1 : 0; }

extern "C" void vrFramePerfAdd(int stage, double ms)
{
    if (stage >= 0 && stage < 3) s_stageMs[stage] += ms;
}

extern "C" void vrFramePerfFrame(double waitMs, float periodMs, int shouldRender)
{
    static const char *const names[] = { "sim", "render", "redraw", "end" };
    const double now = vrFrameNowMs();
    (void)shouldRender;

    if (!s_perfInit) {
        sfvr_perf_init(&s_perf, 10.0, names, 4);
        s_perfInit = true;
        s_prevFrameStartMs = now;
        memset(s_stageMs, 0, sizeof(s_stageMs));
        return;
    }
    const double period = now - s_prevFrameStartMs;
    s_prevFrameStartMs = now;
    gpu_collect();
    const double busy = period > waitMs ? period - waitMs : 0.0;
    double sim = busy - s_stageMs[VRPERF_RENDER] - s_stageMs[VRPERF_REDRAW] - s_stageMs[VRPERF_END];
    if (sim < 0.0) sim = 0.0;
    const float stages[4] = { (float)sim, (float)s_stageMs[VRPERF_RENDER], (float)s_stageMs[VRPERF_REDRAW],
                              (float)s_stageMs[VRPERF_END] };
    const float mv = (float)(s_stageMs[VRPERF_RENDER] + s_stageMs[VRPERF_REDRAW]);
    const float eye[2] = { mv, mv };   /* one multiview pass draws both eyes, so both fields are that pass */
    const int missed = periodMs > 0.0f && period > 1.5 * periodMs;
    s_waitSumMs += waitMs;
    s_waitFrames++;
    sfvr_perf_add_frame(&s_perf, now / 1000.0, missed, stages, eye, s_gpuMsThisFrame);
    s_gpuMsThisFrame = -1.0f;
    memset(s_stageMs, 0, sizeof(s_stageMs));

    char line[256];
    if (sfvr_perf_poll(&s_perf, now / 1000.0, line, sizeof(line))) {
        FLOG("%s wait=%.2fms period=%.1fms (eye=multiview pass, both eyes; sim=tick+input+audio)", line,
             s_waitFrames ? s_waitSumMs / (double)s_waitFrames : 0.0, periodMs);
        s_waitSumMs = 0.0;
        s_waitFrames = 0;
    }
}

#endif /* GEVR_STEAM_FRAME */
