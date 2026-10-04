/* See vr_watch_terminal.h. Steam Frame build only. */
#ifdef GEVR_STEAM_FRAME

#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_OPENGL_ES 1

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "vr_openxr.h"
#include "vr_frame.h"
#include "vr_settings.h"
#include "vr_screen_math.h"
#include "vr_watch_terminal.h"

extern "C" int gevrVrGripPosePlay(int hand, float pos[3], float quat[4]);   // vr_input.cpp: 0 = the watch hand
extern "C" long long gevrVrQuadSwapchainFormat(void);                       // vr_openxr.cpp

/* ------------------------------------------------------------------------------- state */

static GevrTermPanel s_panel;
static bool s_placed;            // s_panel holds a placement
static bool s_up;                // the paused watch is up (gevrVrWatchScreen)
static bool s_style;             // ... as a terminal (WatchStyle, latched as it came up)
static int s_page = -1;          // the page shown (watch_screen_index), -1: none (opening, closing)
static int s_loggedPage = -2;
static bool s_haveTex;           // a page reached the panel since this open
static bool s_logPlaced;         // the placement is logged with the first page
static float s_anim;             // 0 in the watch .. 1 in place
static float s_dockAnim;
static float s_dockAspect = 4.0f / 3.0f;
static unsigned long long s_xrFrames, s_dockLastXr;
static long long s_lastT;
static float s_wrist[3];
static bool s_haveWrist;
static int s_lastFollow;

// this XR frame's panels: 0 the terminal, 1 the VR settings
static bool s_vis[2];
static float s_pos[2][3], s_quat[2][4], s_size[2][2];

struct TermChain {
    XrSwapchain sc = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    uint32_t w = 0, h = 0;
    bool hasImage = false;
};
static TermChain s_chain[2];

/* ------------------------------------------------------------------------------- style */

extern "C" int gevrVrWatchTerminalStyle(void) { return VrWatchStyle != 0 ? 1 : 0; }

extern "C" void gevrVrWatchPage(int page) { s_page = page; }

extern "C" void gevrTermWatchUp(int up)
{
    if (up && !s_up) {
        s_style = VrWatchStyle != 0;
        s_haveTex = false;
        s_anim = 0.0f;
        s_dockAnim = 0.0f;
        s_logPlaced = true;
        s_loggedPage = -2;
        s_lastFollow = 0;
        if (s_style) {
            LOGI("[vr] watch terminal: up (WatchStyle=1), the panel follows the first page");
        }
    } else if (!up && s_up && s_style) {
        LOGI("[vr] watch terminal: down");
    }
    s_up = up != 0;
}

extern "C" int gevrTermActive(void) { return s_up && s_style ? 1 : 0; }
extern "C" int gevrTermPassWanted(void) { return gevrTermActive(); }
extern "C" int gevrTermCapture(void) { return s_up && s_style && s_page >= 0 ? 1 : 0; }

extern "C" void gevrTermPlaceAt(const float head[3], const float headQuat[4])
{
    gevrTermPlace(&s_panel, head, headQuat);
    s_placed = true;
    if (s_up && s_style && !s_logPlaced) {
        LOGI("[vr] watch terminal re-placed yaw=%.1f (recentre, #%u)", s_panel.yaw * 180.0f / 3.14159265f,
             s_panel.placements);
    }
}

/* ------------------------------------------------------------------------- per XR frame */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

extern "C" void gevrTermFrame(const float head[3], const float headQuat[4], long long displayTimeNs)
{
    s_xrFrames++;
    float dt = s_lastT ? (float)((double)(displayTimeNs - s_lastT) * 1e-9) : 0.0f;
    s_lastT = displayTimeNs;
    dt = clampf(dt, 0.0f, 0.1f);

    {
        float p[3], q[4];
        if (gevrVrGripPosePlay(0, p, q)) {   // the watch hand (the right one when left-handed)
            memcpy(s_wrist, p, sizeof(s_wrist));
            s_haveWrist = true;
        }
    }

    const bool open = s_up && s_style && s_placed && s_page >= 0 && s_haveTex;
    s_anim = open ? clampf(s_anim + dt / GEVR_TERM_OPEN_S, 0.0f, 1.0f)
                  : clampf(s_anim - dt / GEVR_TERM_CLOSE_S, 0.0f, 1.0f);
    const bool dockWanted = open && s_xrFrames - s_dockLastXr < 12 && s_chain[1].hasImage;
    s_dockAnim = dockWanted ? clampf(s_dockAnim + dt / 0.15f, 0.0f, 1.0f)
                            : clampf(s_dockAnim - dt / 0.12f, 0.0f, 1.0f);
    if (s_anim <= 0.0f && !s_up) {
        s_haveTex = false;
    }

    // the lazy re-centre, only while it is in place
    if (open && s_anim >= 1.0f) {
        const int following = gevrTermFollow(&s_panel, head, headQuat, dt);
        if (following != s_lastFollow) {
            LOGI("[vr] watch terminal %s yaw=%.1f", following ? "re-centring: the head turned past 50 deg" : "settled",
                 s_panel.yaw * 180.0f / 3.14159265f);
            s_lastFollow = following;
        }
    }

    s_vis[0] = s_vis[1] = false;
    if (!s_placed || s_anim <= 0.0f || !s_chain[0].hasImage) {
        return;
    }
    float pos[3], quat[4];
    gevrTermPose(&s_panel, pos, quat);
    const float e = gevrTermEase(s_anim);
    const float k = GEVR_TERM_BEAM_MIN + (1.0f - GEVR_TERM_BEAM_MIN) * e;
    for (int i = 0; i < 3; i++) {
        s_pos[0][i] = s_haveWrist ? s_wrist[i] + (pos[i] - s_wrist[i]) * e : pos[i];
    }
    memcpy(s_quat[0], quat, sizeof(quat));
    s_size[0][0] = GEVR_TERM_WIDTH_M * k;
    s_size[0][1] = GEVR_TERM_HEIGHT_M * k;
    s_vis[0] = true;

    if (s_dockAnim > 0.0f && s_chain[1].hasImage) {
        float dpos[3], dquat[4], dsize[2];
        gevrTermDockPose(&s_panel, s_dockAspect, dpos, dquat, dsize);
        const float de = gevrTermEase(s_dockAnim) * e;
        const float dk = 0.3f + 0.7f * de;
        // it swings out of the terminal's right edge
        float hinge[3];
        gevrQuadPoint(s_pos[0], s_quat[0], s_size[0][0], s_size[0][1], 1.0f, 0.5f, hinge);
        for (int i = 0; i < 3; i++) {
            s_pos[1][i] = hinge[i] + (dpos[i] - hinge[i]) * de;
        }
        memcpy(s_quat[1], dquat, sizeof(dquat));
        s_size[1][0] = dsize[0] * dk;
        s_size[1][1] = dsize[1] * dk;
        s_vis[1] = true;
    }
}

/* ---------------------------------------------------------------------------- swapchains */

static bool term_chain_ensure(TermChain &c, uint32_t w, uint32_t h, const char *what)
{
    if (c.sc != XR_NULL_HANDLE && c.w == w && c.h == h) {
        return true;
    }
    if (c.sc != XR_NULL_HANDLE) {
        xrDestroySwapchain(c.sc);
        c.sc = XR_NULL_HANDLE;
        c.images.clear();
        c.hasImage = false;
    }
    if (g_vrState.session == XR_NULL_HANDLE) {
        return false;
    }
    XrSwapchainCreateInfo info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;   // as the screen's
    info.format      = gevrVrQuadSwapchainFormat();
    info.sampleCount = 1;
    info.width       = w;
    info.height      = h;
    info.faceCount   = 1;
    info.arraySize   = 1;
    info.mipCount    = 1;
    if (XR_FAILED(xrCreateSwapchain(g_vrState.session, &info, &c.sc))) {
        LOGE("[vr] watch terminal: xrCreateSwapchain %ux%u (%s) failed", w, h, what);
        c.sc = XR_NULL_HANDLE;
        return false;
    }
    uint32_t n = 0;
    xrEnumerateSwapchainImages(c.sc, 0, &n, nullptr);
    c.images.resize(n, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
    xrEnumerateSwapchainImages(c.sc, n, &n, reinterpret_cast<XrSwapchainImageBaseHeader *>(c.images.data()));
    c.w = w;
    c.h = h;
    LOGI("[vr] watch terminal: %s swapchain %ux%u (format 0x%llx)", what, w, h, (unsigned long long)info.format);
    return true;
}

extern "C" void gevrTermDestroy(void)
{
    for (TermChain &c : s_chain) {
        if (c.sc != XR_NULL_HANDLE) {
            xrDestroySwapchain(c.sc);
        }
        c = TermChain();
    }
    s_vis[0] = s_vis[1] = false;
}

/*
 * Copy (and scale) a GL image into a swapchain image: `attach` binds the source to the bound
 * read framebuffer. The GL state touched is put back: framebuffer bindings, scissor, clear colour,
 * colour mask. `frame` adds the terminal's thin green frame. `dumpName`: the dump's file when one
 * is due this frame.
 */
template <typename Attach>
static bool term_copy(TermChain &c, Attach attach, int sw, int sh, bool frame, const char *dumpName)
{
    uint32_t idx = 0;
    XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(xrAcquireSwapchainImage(c.sc, &acquireInfo, &idx))) {
        return false;
    }
    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(c.sc, &waitInfo);

    GLint prevRead = 0, prevDraw = 0, prevScissorBox[4];
    GLfloat prevClear[4];
    GLboolean prevMask[4];
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
    glGetIntegerv(GL_SCISSOR_BOX, prevScissorBox);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClear);
    glGetBooleanv(GL_COLOR_WRITEMASK, prevMask);

    static GLuint srcFbo = 0, dstFbo = 0;
    if (!srcFbo) glGenFramebuffers(1, &srcFbo);
    if (!dstFbo) glGenFramebuffers(1, &dstFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
    attach();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c.images[idx].image, 0);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    const bool scaled = sw != (int)c.w || sh != (int)c.h;
    glBlitFramebuffer(0, 0, sw, sh, 0, 0, (GLint)c.w, (GLint)c.h, GL_COLOR_BUFFER_BIT, scaled ? GL_LINEAR : GL_NEAREST);

    if (frame) {
        // the Q-branch frame: 3 px of the LCD's green round the edge, alpha 1 (the layer is opaque)
        const GLint b = 3, W = (GLint)c.w, H = (GLint)c.h;
        const GLint strips[4][4] = { { 0, 0, W, b }, { 0, H - b, W, b }, { 0, 0, b, H }, { W - b, 0, b, H } };
        glEnable(GL_SCISSOR_TEST);
        glClearColor(0.44f, 0.90f, 0.65f, 1.0f);
        for (const auto &s : strips) {
            glScissor(s[0], s[1], s[2], s[3]);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        glDisable(GL_SCISSOR_TEST);
    }
    if (dumpName && vrFrameDumpDue()) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, dstFbo);
        vrFrameDumpPanel(dumpName, (int)c.w, (int)c.h);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
    glScissor(prevScissorBox[0], prevScissorBox[1], prevScissorBox[2], prevScissorBox[3]);
    glClearColor(prevClear[0], prevClear[1], prevClear[2], prevClear[3]);
    glColorMask(prevMask[0], prevMask[1], prevMask[2], prevMask[3]);
    if (scissor) glEnable(GL_SCISSOR_TEST);

    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(c.sc, &releaseInfo);
    c.hasImage = true;
    return true;
}

extern "C" int gevrTermPresent(unsigned srcArrayTex, int w, int h, const float crop[4], int measured)
{
    if (!gevrTermCapture() || srcArrayTex == 0 || w <= 0 || h <= 0) {
        return 0;
    }
    if (!term_chain_ensure(s_chain[0], GEVR_TERM_TEX_W, GEVR_TERM_TEX_H, "panel")) {
        return 0;
    }
    if (!term_copy(s_chain[0],
                   [&] { glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, (GLuint)srcArrayTex, 0, 0); },
                   w, h, true, "vr-watch.bmp")) {
        return 0;
    }
    s_haveTex = true;
    if (s_logPlaced) {
        s_logPlaced = false;
        LOGI("[vr] watch terminal placed yaw=%.1f dist=%.2f size=%.2fx%.2f tex=%dx%d page=%d "
             "(centre %.2f m below the eyes, facing them; rendered %dx%d from native crop %.0f,%.0f %.0fx%.0f, %s; #%u)",
             s_panel.yaw * 180.0f / 3.14159265f, GEVR_TERM_DIST_M, GEVR_TERM_WIDTH_M, GEVR_TERM_HEIGHT_M,
             GEVR_TERM_TEX_W, GEVR_TERM_TEX_H, s_page, GEVR_TERM_DROP_M, w, h, crop[0], crop[1], crop[2], crop[3],
             measured ? "the watch face" : "no face measured: the whole frame", s_panel.placements);
        vrFrameDumpWatchPlaced();   // vr-diag.ini dump_watch_frame: the eyes and vr-watch.bmp
        s_loggedPage = s_page;
    } else if (s_page != s_loggedPage) {
        LOGI("[vr] watch terminal page=%d", s_page);
        s_loggedPage = s_page;
    }
    return 1;
}

extern "C" int gevrTermDockPresent(unsigned srcTex2d, int w, int h)
{
    if (!gevrTermActive() || srcTex2d == 0 || w <= 0 || h <= 0) {
        return 0;
    }
    if (!term_chain_ensure(s_chain[1], (uint32_t)w, (uint32_t)h, "VR settings dock")) {
        return 0;
    }
    if (!term_copy(s_chain[1],
                   [&] { glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, (GLuint)srcTex2d, 0); },
                   w, h, false, "vr-dock.bmp")) {
        return 0;
    }
    if (s_xrFrames - s_dockLastXr >= 12) {
        LOGI("[vr] watch terminal: VR settings docked to the right (%dx%d)", w, h);
    }
    s_dockAspect = (float)w / (float)h;
    s_dockLastXr = s_xrFrames;
    return 1;
}

/* ------------------------------------------------------------------------- compositor */

int gevrTermLayers(XrSpace space, XrCompositionLayerQuad out[2])
{
    int n = 0;
    for (int i = 0; i < 2; i++) {
        if (!s_vis[i] || s_chain[i].sc == XR_NULL_HANDLE) {
            continue;
        }
        XrCompositionLayerQuad &q = out[n++];
        q = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        q.layerFlags = 0;   // opaque: the game's alpha is not to be trusted
        q.space = space;
        q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        q.subImage.swapchain = s_chain[i].sc;
        q.subImage.imageArrayIndex = 0;
        q.subImage.imageRect.offset = {0, 0};
        q.subImage.imageRect.extent = {(int32_t)s_chain[i].w, (int32_t)s_chain[i].h};
        q.pose.position = {s_pos[i][0], s_pos[i][1], s_pos[i][2]};
        q.pose.orientation = {s_quat[i][0], s_quat[i][1], s_quat[i][2], s_quat[i][3]};
        q.size = {s_size[i][0], s_size[i][1]};
    }
    return n;
}

/* ---------------------------------------------------------------------------- the laser */

extern "C" int gevrTermPointerOwned(void) { return gevrTermActive(); }

extern "C" int gevrTermHit(const float o[3], const float d[3], float *u, float *v)
{
    // only the VR settings take the laser; the watch's pages are the game's, driven by the pad
    if (!s_vis[1] || s_dockAnim < 1.0f) {
        return 0;
    }
    return gevrQuadHit(s_pos[1], s_quat[1], s_size[1][0], s_size[1][1], o, d, u, v);
}

extern "C" void gevrTermUvToWorld(float u, float v, float out[3])
{
    const int i = s_vis[1] ? 1 : 0;
    gevrQuadPoint(s_pos[i], s_quat[i], s_size[i][0], s_size[i][1], u, v, out);
}

extern "C" int gevrTermCorners(float corners[2][4][3])
{
    static const float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    int n = 0;
    for (int i = 0; i < 2; i++) {
        if (!s_vis[i]) {
            continue;
        }
        for (int k = 0; k < 4; k++) {
            gevrQuadPoint(s_pos[i], s_quat[i], s_size[i][0], s_size[i][1], uv[k][0], uv[k][1], corners[n][k]);
        }
        n++;
    }
    return n;
}

#endif /* GEVR_STEAM_FRAME */
