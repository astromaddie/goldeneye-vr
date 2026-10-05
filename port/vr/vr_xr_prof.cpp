/* See vr_xr_prof.h. Everything here is Steam Frame only. */
#ifdef GEVR_STEAM_FRAME

#include "vr_xr_prof.h"
#include "vr_diag.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <openxr/openxr.h>

/* ------------------------------------------------------------------ counters */

namespace {

double nowMs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

double cpuMs(clockid_t clk)
{
    struct timespec ts;
    clock_gettime(clk, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

enum { K_ACQ, K_WAITIMG, K_REL, K_END, K_SYNC, K_STATE, K_LOCATE, K_N };

struct Window {
    unsigned frames;
    unsigned calls[K_N];
    double ms[K_N];
    unsigned layers;
    unsigned counts[VRPROF_COUNT_N];
    double threadCpuMs, procCpuMs;
    double threadCpuMax;
    double gpuAuxMs;
    unsigned gpuAuxN;
};

Window w;
double lastThreadCpu = -1.0, lastProcCpu = -1.0;

inline void add(int k, double t0)
{
    w.calls[k]++;
    w.ms[k] += nowMs() - t0;
}

}  // namespace

extern "C" void vrProfCount(int what)
{
    if (what >= 0 && what < VRPROF_COUNT_N) w.counts[what]++;
}

extern "C" void vrProfFrame(void)
{
    const double t = cpuMs(CLOCK_THREAD_CPUTIME_ID), p = cpuMs(CLOCK_PROCESS_CPUTIME_ID);
    if (lastThreadCpu >= 0.0) {
        const double d = t - lastThreadCpu;
        w.threadCpuMs += d;
        if (d > w.threadCpuMax) w.threadCpuMax = d;
        w.procCpuMs += p - lastProcCpu;
        w.frames++;
    }
    lastThreadCpu = t;
    lastProcCpu = p;
}

extern "C" void vrProfLine(char *out, int size)
{
    const double f = w.frames ? (double)w.frames : 1.0;
    const unsigned cycles = w.calls[K_ACQ];
    char gpu[32] = "n/a";
    if (w.gpuAuxN) snprintf(gpu, sizeof(gpu), "%.3fms/frame", w.gpuAuxMs / f);
    snprintf(out, (size_t)size,
             "[vr-xr] frames=%u cycles=%.2f/frame (acquire %.3f wait %.3f release %.3f ms/frame) layers=%.2f "
             "endframe=%.3fms sync=%.3fms (%.2f/frame) states=%.2f/frame %.3fms locates=%.2f/frame %.3fms "
             "thread_cpu=%.2fms (max %.1f) process_cpu=%.2fms hud_copies=%.2f/frame hud_skipped=%.2f/frame "
             "readbacks=%u input_reused=%.2f/frame gpu_aux=%s",
             w.frames, cycles / f, w.ms[K_ACQ] / f, w.ms[K_WAITIMG] / f, w.ms[K_REL] / f, w.layers / f,
             w.ms[K_END] / f, w.ms[K_SYNC] / f, w.calls[K_SYNC] / f, w.calls[K_STATE] / f, w.ms[K_STATE] / f,
             w.calls[K_LOCATE] / f, w.ms[K_LOCATE] / f, w.threadCpuMs / f, w.threadCpuMax, w.procCpuMs / f,
             w.counts[VRPROF_HUD_COPY] / f, w.counts[VRPROF_HUD_SKIPPED] / f, w.counts[VRPROF_READBACK],
             w.counts[VRPROF_INPUT_SKIPPED] / f, gpu);
    memset(&w, 0, sizeof(w));
}

/* ------------------------------------------------------- GPU timer, aux work */

#define GL_TIME_ELAPSED_EXT 0x88BF
#define GL_GPU_DISJOINT_EXT 0x8FBB
namespace {
typedef void (*PFN_genq)(GLsizei, GLuint *);
typedef void (*PFN_beginq)(GLenum, GLuint);
typedef void (*PFN_endq)(GLenum);
typedef void (*PFN_getqui)(GLuint, GLenum, GLuint *);
typedef void (*PFN_getqui64)(GLuint, GLenum, unsigned long long *);
PFN_genq pGenQ;
PFN_beginq pBeginQ;
PFN_endq pEndQ;
PFN_getqui pGetQui;
PFN_getqui64 pGetQui64;
bool gpuTried, gpuOk, qOpen;
const int NQ = 16;
GLuint queries[NQ];
bool pending[NQ];
int head;

void gpuInit()
{
    if (gpuTried) return;
    gpuTried = true;
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    if (!ext || !strstr(ext, "GL_EXT_disjoint_timer_query")) return;
    pGenQ = (PFN_genq)eglGetProcAddress("glGenQueriesEXT");
    pBeginQ = (PFN_beginq)eglGetProcAddress("glBeginQueryEXT");
    pEndQ = (PFN_endq)eglGetProcAddress("glEndQueryEXT");
    pGetQui = (PFN_getqui)eglGetProcAddress("glGetQueryObjectuivEXT");
    pGetQui64 = (PFN_getqui64)eglGetProcAddress("glGetQueryObjectui64vEXT");
    if (!pGenQ || !pBeginQ || !pEndQ || !pGetQui || !pGetQui64) return;
    pGenQ(NQ, queries);
    gpuOk = true;
}

void gpuCollect()
{
    GLint disjoint = 0;
    glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
    for (int i = 0; i < NQ; i++) {
        if (!pending[i]) continue;
        GLuint avail = 0;
        pGetQui(queries[i], 0x8867 /* GL_QUERY_RESULT_AVAILABLE_EXT */, &avail);
        if (!avail) continue;
        unsigned long long ns = 0;
        pGetQui64(queries[i], 0x8866 /* GL_QUERY_RESULT_EXT */, &ns);
        pending[i] = false;
        if (!disjoint) {
            w.gpuAuxMs += ns / 1.0e6;
            w.gpuAuxN++;
        }
    }
}
}  // namespace

extern "C" void vrProfGpuAuxBegin(void)
{
    if (!gevrDiag()->timing) return;
    gpuInit();
    if (!gpuOk || qOpen) return;
    gpuCollect();
    if (pending[head]) return;
    pBeginQ(GL_TIME_ELAPSED_EXT, queries[head]);
    qOpen = true;
}

extern "C" void vrProfGpuAuxEnd(void)
{
    if (!gpuOk || !qOpen) return;
    pEndQ(GL_TIME_ELAPSED_EXT);
    pending[head] = true;
    head = (head + 1) % NQ;
    qOpen = false;
}

/* --------------------------------------------- the wrapped OpenXR entry points */
/* CMakeLists.txt links the Frame build with -Wl,--wrap=<each of these>: every call from this
 * library lands here, and __real_<name> is the loader's. */

extern "C" {
XrResult __real_xrAcquireSwapchainImage(XrSwapchain, const XrSwapchainImageAcquireInfo *, uint32_t *);
XrResult __real_xrWaitSwapchainImage(XrSwapchain, const XrSwapchainImageWaitInfo *);
XrResult __real_xrReleaseSwapchainImage(XrSwapchain, const XrSwapchainImageReleaseInfo *);
XrResult __real_xrEndFrame(XrSession, const XrFrameEndInfo *);
XrResult __real_xrSyncActions(XrSession, const XrActionsSyncInfo *);
XrResult __real_xrGetActionStateBoolean(XrSession, const XrActionStateGetInfo *, XrActionStateBoolean *);
XrResult __real_xrGetActionStateFloat(XrSession, const XrActionStateGetInfo *, XrActionStateFloat *);
XrResult __real_xrGetActionStateVector2f(XrSession, const XrActionStateGetInfo *, XrActionStateVector2f *);
XrResult __real_xrGetActionStatePose(XrSession, const XrActionStateGetInfo *, XrActionStatePose *);
XrResult __real_xrLocateSpace(XrSpace, XrSpace, XrTime, XrSpaceLocation *);

XrResult __wrap_xrAcquireSwapchainImage(XrSwapchain s, const XrSwapchainImageAcquireInfo *i, uint32_t *idx)
{
    const double t0 = nowMs();
    const XrResult r = __real_xrAcquireSwapchainImage(s, i, idx);
    add(K_ACQ, t0);
    return r;
}

XrResult __wrap_xrWaitSwapchainImage(XrSwapchain s, const XrSwapchainImageWaitInfo *i)
{
    const double t0 = nowMs();
    const XrResult r = __real_xrWaitSwapchainImage(s, i);
    add(K_WAITIMG, t0);
    return r;
}

XrResult __wrap_xrReleaseSwapchainImage(XrSwapchain s, const XrSwapchainImageReleaseInfo *i)
{
    const double t0 = nowMs();
    const XrResult r = __real_xrReleaseSwapchainImage(s, i);
    add(K_REL, t0);
    return r;
}

XrResult __wrap_xrEndFrame(XrSession s, const XrFrameEndInfo *i)
{
    const double t0 = nowMs();
    const XrResult r = __real_xrEndFrame(s, i);
    add(K_END, t0);
    if (i) w.layers += i->layerCount;
    return r;
}

XrResult __wrap_xrSyncActions(XrSession s, const XrActionsSyncInfo *i)
{
    const double t0 = nowMs();
    const XrResult r = __real_xrSyncActions(s, i);
    add(K_SYNC, t0);
    return r;
}

XrResult __wrap_xrGetActionStateBoolean(XrSession s, const XrActionStateGetInfo *i, XrActionStateBoolean *o)
{
    const double t0 = nowMs();
    const XrResult r = __real_xrGetActionStateBoolean(s, i, o);
    add(K_STATE, t0);
    return r;
}

XrResult __wrap_xrGetActionStateFloat(XrSession s, const XrActionStateGetInfo *i, XrActionStateFloat *o)
{
    const double t0 = nowMs();
    const XrResult r = __real_xrGetActionStateFloat(s, i, o);
    add(K_STATE, t0);
    return r;
}

XrResult __wrap_xrGetActionStateVector2f(XrSession s, const XrActionStateGetInfo *i, XrActionStateVector2f *o)
{
    const double t0 = nowMs();
    const XrResult r = __real_xrGetActionStateVector2f(s, i, o);
    add(K_STATE, t0);
    return r;
}

XrResult __wrap_xrGetActionStatePose(XrSession s, const XrActionStateGetInfo *i, XrActionStatePose *o)
{
    const double t0 = nowMs();
    const XrResult r = __real_xrGetActionStatePose(s, i, o);
    add(K_STATE, t0);
    return r;
}

XrResult __wrap_xrLocateSpace(XrSpace a, XrSpace b, XrTime t, XrSpaceLocation *o)
{
    const double t0 = nowMs();
    const XrResult r = __real_xrLocateSpace(a, b, t, o);
    add(K_LOCATE, t0);
    return r;
}
}  // extern "C"

#endif /* GEVR_STEAM_FRAME */
