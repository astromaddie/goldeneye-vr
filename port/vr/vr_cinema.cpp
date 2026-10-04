/* See vr_cinema.h and vr_cinema_math.h. Steam Frame build only. */
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
#include "vr_cinema.h"
#include "vr_cinema_math.h"
#include "vr_diag.h"
#include "vr_frame.h"

extern "C" long long gevrVrQuadSwapchainFormat(void);   // vr_openxr.cpp: the virtual screen's format
extern "C" long long gevrVrDisplayTimeNs(void);         // vr_openxr.cpp: this XR frame's display time

typedef void (*GevrFramebufferTextureMultiviewOVRProc)(GLenum, GLenum, GLuint, GLint, GLint, GLsizei);
extern GevrFramebufferTextureMultiviewOVRProc glFramebufferTextureMultiviewOVR;   // gfx_opengl.cpp

/* ------------------------------------------------------------------------------- state */

static int s_state;                  // gevrVrCinema: 0 none, 1 flat, 2 cinema
static float s_unitsPerMetre = 100.0f;
static int s_cameraMode;
static unsigned s_cutscenes;         // cutscenes on the cinema so far (logs)
static unsigned s_gameFrames;        // game frames of this cutscene
static bool s_needPlace;             // place the screen at the next XR frame (start, recentre)
static bool s_placed;
static unsigned s_placements;
static GevrCinemaScreen s_screen;
static float s_aspect = 4.0f / 3.0f;
static float s_ipd = 0.064f;         // the runtime's, from the last XR frame's eyes
static float s_halfSepM = 0.5f * GEVR_CINEMA_SEP_M;   // this game frame's (clamped) half separation
static float s_lastP00;
static bool s_clampLogged;
static bool s_behindLogged;
static bool s_chainFailed;           // the cinema's swapchain could not be made: this cutscene is flat

static long long s_fadeStartNs;      // the transition cover started (display time), 0 = none
static bool s_fadeActive;

// the cinema's swapchain: two layers (left, right eye), the picture over the band
struct CinemaChain {
    XrSwapchain sc = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    uint32_t w = 0, h = 0, picH = 0, bandH = 0;
    bool hasImage = false;
};
static CinemaChain s_chain;

// the band under the picture: a two-view target (the game's programs all declare two views)
static GLuint s_bandFbo, s_bandColor, s_bandDepth;
static int s_bandW, s_bandH;
static bool s_passOn, s_bandOn, s_bandOk;
static GLint s_picFbo;               // the screen target, bound for the cinema pass

// the transition cover: a small black image whose alpha is the fade
static XrSwapchain s_fadeSc = XR_NULL_HANDLE;
static std::vector<XrSwapchainImageOpenGLESKHR> s_fadeImages;
static float s_fadeWritten = -1.0f;
#define GEVR_FADE_TEX 16

// [vr-cinema] once every 2 s of cutscene
static unsigned s_xrFrames;
static float s_lastFov[2][4];

/* ------------------------------------------------------------------------------- game side */

static float cinema_half_sep_m(void)
{
    const float mm = gevrDiag()->cinema_sep_mm;
    return 0.5f * (mm > 0.0f ? mm * 0.001f : GEVR_CINEMA_SEP_M);
}

extern "C" void gevrVrCinema(int state, float unitsPerMetre, int cameraMode)
{
    state = state < 0 ? 0 : state > 2 ? 2 : state;
    if (state == 0) {
        s_chainFailed = false;   // the next cutscene tries again
    } else if (state == 2 && s_chainFailed) {
        state = 1;
    }
    if (unitsPerMetre > 0.0f) {
        s_unitsPerMetre = unitsPerMetre;
    }
    s_cameraMode = cameraMode;
    if (state != s_state) {
        // every change into or out of a cutscene fades from black (standard.md "Transitions")
        s_fadeStartNs = gevrVrDisplayTimeNs();
        s_fadeActive = s_fadeStartNs != 0;
        if (state == 2) {
            s_cutscenes++;
            s_gameFrames = 0;
            s_needPlace = true;
            s_placed = false;
            s_clampLogged = false;
            s_behindLogged = false;
            s_chain.hasImage = false;
            LOGI("[vr] cinema 3D: cutscene #%u starts (camera mode %d, %.1f view units per metre)", s_cutscenes,
                 cameraMode, s_unitsPerMetre);
        } else if (s_state == 2) {
            LOGI("[vr] cinema 3D: cutscene #%u ends after %u game frames (%s)", s_cutscenes, s_gameFrames,
                 state == 1 ? "flat on the screen" : "back to play");
        }
        s_state = state;
    }
    if (s_state == 2) {
        s_gameFrames++;
    }
}

extern "C" int gevrCinemaActive(void) { return s_state == 2 ? 1 : 0; }

extern "C" int gevrCinemaPassOffsets(float p00, float out[8])
{
    if (s_state != 2 || !(p00 > 0.0f)) {
        return 0;
    }
    const float want = cinema_half_sep_m();
    const float half = gevrCinemaSafeHalfSep(p00, want, GEVR_CINEMA_DIST_M, GEVR_CINEMA_WIDTH_M, s_ipd);
    if (half < want - 1e-5f && !s_clampLogged) {
        s_clampLogged = true;
        LOGI("[vr] cinema 3D: separation held to %.1f mm (P00 %.2f, a zoomed camera: the far field kept at %.2f x IPD "
             "%.1f mm on the screen)", 2000.0f * half, p00, GEVR_CINEMA_FAR_MAX, s_ipd * 1000.0f);
    }
    s_halfSepM = half;
    s_lastP00 = p00;
    gevrCinemaShear(p00, half * s_unitsPerMetre, GEVR_CINEMA_DIST_M * s_unitsPerMetre, out);
    return 1;
}

/* ------------------------------------------------------------------------------- the band */

static void band_release(void)
{
    if (s_bandFbo) { glDeleteFramebuffers(1, &s_bandFbo); s_bandFbo = 0; }
    if (s_bandColor) { glDeleteTextures(1, &s_bandColor); s_bandColor = 0; }
    if (s_bandDepth) { glDeleteTextures(1, &s_bandDepth); s_bandDepth = 0; }
    s_bandW = s_bandH = 0;
}

static bool band_ensure(int w, int h)
{
    if (s_bandFbo && s_bandW == w && s_bandH == h) {
        return true;
    }
    if (!glFramebufferTextureMultiviewOVR || w <= 0 || h <= 0) {
        return false;
    }
    band_release();
    GLint prevArray = 0, prevFbo = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &prevArray);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGenTextures(1, &s_bandColor);
    glBindTexture(GL_TEXTURE_2D_ARRAY, s_bandColor);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, w, h, 2);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenTextures(1, &s_bandDepth);
    glBindTexture(GL_TEXTURE_2D_ARRAY, s_bandDepth);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_DEPTH24_STENCIL8, w, h, 2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)prevArray);
    glGenFramebuffers(1, &s_bandFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_bandFbo);
    glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, s_bandColor, 0, 0, 2);
    glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, s_bandDepth, 0, 0, 2);
    const GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("[vr] cinema 3D: band target %dx%d incomplete (0x%x): the bottom messages stay in the picture", w, h,
             (unsigned)st);
        band_release();
        return false;
    }
    s_bandW = w;
    s_bandH = h;
    LOGI("[vr] cinema 3D: band target %dx%d, two views", w, h);
    return true;
}

static int band_rows(int picH)
{
    return ((int)lroundf((float)picH * GEVR_CINEMA_BAND_FRAC) + 1) & ~1;
}

extern "C" int gevrCinemaPassBegin(int w, int h)
{
    if (s_state != 2) {
        return 0;
    }
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s_picFbo);
    s_passOn = true;
    s_bandOn = false;
    s_bandOk = band_ensure(w, band_rows(h));
    if (!s_bandOk) {
        return 0;
    }
    // cleared to opaque black; every GL state touched is put back
    GLint vp[4], sc[4];
    GLfloat clear[4];
    GLboolean depthMask = GL_TRUE, mask[4];
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_SCISSOR_BOX, sc);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    glBindFramebuffer(GL_FRAMEBUFFER, s_bandFbo);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xff);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)s_picFbo);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    glDepthMask(depthMask);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glScissor(sc[0], sc[1], sc[2], sc[3]);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    return 1;
}

/*
 * The band takes the bottom GEVR_CINEMA_BAND_FRAC of the game's screen: it is a target as wide as
 * the picture and that many rows tall, so with the viewport and scissor fast3d computes for the
 * picture (GL rows from the bottom) the screen's bottom strip lands in it. bondview2.c draws the
 * bottom messages there in cinema frames.
 */
extern "C" void gevrCinemaBand(int on)
{
    if (!s_passOn || !s_bandOk || (on != 0) == s_bandOn) {
        return;
    }
    s_bandOn = on != 0;
    glBindFramebuffer(GL_FRAMEBUFFER, s_bandOn ? s_bandFbo : (GLuint)s_picFbo);
}

extern "C" void gevrCinemaPassEnd(void)
{
    if (!s_passOn) {
        return;
    }
    if (s_bandOn) {
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)s_picFbo);
        s_bandOn = false;
    }
    s_passOn = false;
}

/* ------------------------------------------------------------------------------- present */

static bool chain_ensure(uint32_t w, uint32_t picH, uint32_t bandH)
{
    const uint32_t h = picH + bandH;
    if (s_chain.sc != XR_NULL_HANDLE && s_chain.w == w && s_chain.h == h && s_chain.bandH == bandH) {
        return true;
    }
    if (s_chain.sc != XR_NULL_HANDLE) {
        xrDestroySwapchain(s_chain.sc);
        s_chain = CinemaChain();
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
    info.arraySize   = 2;   // the left eye's picture, the right eye's
    info.mipCount    = 1;
    if (XR_FAILED(xrCreateSwapchain(g_vrState.session, &info, &s_chain.sc))) {
        LOGE("[vr] cinema 3D: xrCreateSwapchain %ux%u x2 failed: held flat (the virtual screen)", w, h);
        s_chain.sc = XR_NULL_HANDLE;
        return false;
    }
    uint32_t n = 0;
    xrEnumerateSwapchainImages(s_chain.sc, 0, &n, nullptr);
    s_chain.images.resize(n, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
    xrEnumerateSwapchainImages(s_chain.sc, n, &n, reinterpret_cast<XrSwapchainImageBaseHeader *>(s_chain.images.data()));
    s_chain.w = w;
    s_chain.h = h;
    s_chain.picH = picH;
    s_chain.bandH = bandH;
    LOGI("[vr] cinema 3D: swapchain %ux%u, two layers (picture %u rows over a %u-row band; format 0x%llx)", w, h, picH,
         bandH, (unsigned long long)info.format);
    return true;
}

extern "C" int gevrCinemaPresent(unsigned srcArrayTex, int w, int h)
{
    if (s_state != 2 || srcArrayTex == 0 || w <= 0 || h <= 0) {
        return 0;
    }
    const uint32_t bandH = (uint32_t)band_rows(h);
    if (!chain_ensure((uint32_t)w, (uint32_t)h, bandH)) {
        s_chainFailed = true;   // held flat for the rest of this cutscene (logged in chain_ensure)
        s_state = 1;
        return 0;
    }
    const float aspect = (float)w / (float)h;
    if (fabsf(aspect - s_aspect) > 1e-3f) {
        s_aspect = aspect;
        if (s_placed) {
            gevrCinemaSetAspect(&s_screen, s_aspect);
        }
    }

    uint32_t idx = 0;
    XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(xrAcquireSwapchainImage(s_chain.sc, &acquireInfo, &idx))) {
        return 0;
    }
    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(s_chain.sc, &waitInfo);

    GLint prevRead = 0, prevDraw = 0;
    GLfloat prevClear[4];
    GLboolean prevMask[4];
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClear);
    glGetBooleanv(GL_COLOR_WRITEMASK, prevMask);
    static GLuint srcFbo = 0, dstFbo = 0;
    if (!srcFbo) glGenFramebuffers(1, &srcFbo);
    if (!dstFbo) glGenFramebuffers(1, &dstFbo);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    const GLuint image = s_chain.images[idx].image;
    const GLint band = (GLint)bandH;
    for (int layer = 0; layer < 2; layer++) {
        // GL rows run up from the bottom: the band in rows 0..band, the picture above it
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
        glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, image, 0, layer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
        glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, (GLuint)srcArrayTex, 0, layer);
        glBlitFramebuffer(0, 0, w, h, 0, band, w, band + h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        if (s_bandOk && s_bandColor && s_bandW == w && s_bandH == band) {
            glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, s_bandColor, 0, layer);
            glBlitFramebuffer(0, 0, w, band, 0, 0, w, band, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        } else {
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, w, band);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_SCISSOR_TEST);
        }
        if (vrFrameDumpDue()) {
            // vr-dumps/vr-cinema0.bmp, vr-cinema1.bmp: each eye's image as the compositor gets it
            glBindFramebuffer(GL_READ_FRAMEBUFFER, dstFbo);
            vrFrameDumpPanel(layer == 0 ? "vr-cinema0.bmp" : "vr-cinema1.bmp", w, (int)s_chain.h);
            if (layer == 1) {
                LOGI("[vr-dump] cinema: vr-cinema0.bmp/vr-cinema1.bmp %dx%u (picture %d rows over a %d-row band), "
                     "yaw=%.1f sep=%.1fmm P00=%.3f upm=%.1f fov L(%.3f %.3f %.3f %.3f) R(%.3f %.3f %.3f %.3f) ipd=%.1fmm",
                     w, s_chain.h, h, band, s_screen.yaw * 57.29578f, 2000.0f * s_halfSepM, s_lastP00, s_unitsPerMetre,
                     s_lastFov[0][0], s_lastFov[0][1], s_lastFov[0][2], s_lastFov[0][3], s_lastFov[1][0],
                     s_lastFov[1][1], s_lastFov[1][2], s_lastFov[1][3], s_ipd * 1000.0f);
            }
        }
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
    glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0, 0, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
    glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0, 0, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
    glClearColor(prevClear[0], prevClear[1], prevClear[2], prevClear[3]);
    glColorMask(prevMask[0], prevMask[1], prevMask[2], prevMask[3]);
    if (scissor) glEnable(GL_SCISSOR_TEST);

    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(s_chain.sc, &releaseInfo);
    s_chain.hasImage = true;
    return 1;
}

/* ------------------------------------------------------------------------------- per XR frame */

extern "C" void gevrCinemaRecenter(void)
{
    if (s_state == 2) {
        s_needPlace = true;
    }
}

static void cinema_place(const XrView eyes[2])
{
    const XrPosef &l = eyes[0].pose, &r = eyes[1].pose;
    const float head[3] = { (l.position.x + r.position.x) * 0.5f, (l.position.y + r.position.y) * 0.5f,
                            (l.position.z + r.position.z) * 0.5f };
    const float q[4] = { l.orientation.x, l.orientation.y, l.orientation.z, l.orientation.w };
    gevrCinemaPlace(head, q, s_aspect, &s_screen);
    const bool first = !s_placed;
    s_placed = true;
    s_needPlace = false;
    s_placements++;
    LOGI("[vr] cinema 3D: screen placed yaw=%.1f dist=%.2f width=%.2f sep=%.0fmm aspect=%.3f (picture %.2f x %.2f m, "
         "band %.2f m under it, its centre %.2f m below the eyes; zero parallax at %.1f m in the game; %s, #%u)",
         s_screen.yaw * 57.29578f, s_screen.dist, s_screen.width, 2000.0f * cinema_half_sep_m(), s_aspect,
         s_screen.width, s_screen.picH, s_screen.bandH, GEVR_CINEMA_DROP_M, GEVR_CINEMA_DIST_M,
         first ? "cutscene start" : "recentre", s_placements);
    if (first) {
        vrFrameDumpCinemaPlaced();   // vr-diag.ini dump_cinema_frame
    }
}

bool gevrCinemaLayer(XrSpace space, const XrView eyes[2], XrCompositionLayerProjection *layer,
                     XrCompositionLayerProjectionView views[2])
{
    if (s_state != 2 || !s_chain.hasImage || s_chain.sc == XR_NULL_HANDLE) {
        return false;
    }
    {
        const float dx = eyes[1].pose.position.x - eyes[0].pose.position.x;
        const float dy = eyes[1].pose.position.y - eyes[0].pose.position.y;
        const float dz = eyes[1].pose.position.z - eyes[0].pose.position.z;
        const float ipd = sqrtf(dx * dx + dy * dy + dz * dz);
        if (ipd > 0.04f && ipd < 0.09f) {
            s_ipd = ipd;
        }
    }
    if (s_needPlace || !s_placed) {
        cinema_place(eyes);
    }
    for (int e = 0; e < 2; e++) {
        const float eye[3] = { eyes[e].pose.position.x, eyes[e].pose.position.y, eyes[e].pose.position.z };
        float fov[4], depth = 0.0f;
        if (!gevrCinemaEyeFov(&s_screen, eye, fov, &depth)) {
            if (!s_behindLogged) {
                s_behindLogged = true;
                LOGI("[vr] cinema 3D: eye %d is %.2f m from the screen's plane (not in front): no cinema layer this "
                     "frame (recentre places it again)", e, depth);
            }
            return false;
        }
        memcpy(s_lastFov[e], fov, sizeof(fov));
        views[e] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        views[e].pose.orientation = { s_screen.quat[0], s_screen.quat[1], s_screen.quat[2], s_screen.quat[3] };
        views[e].pose.position = eyes[e].pose.position;
        views[e].fov.angleLeft = fov[0];
        views[e].fov.angleRight = fov[1];
        views[e].fov.angleUp = fov[2];
        views[e].fov.angleDown = fov[3];
        views[e].subImage.swapchain = s_chain.sc;
        views[e].subImage.imageRect.offset = {0, 0};
        views[e].subImage.imageRect.extent = {(int32_t)s_chain.w, (int32_t)s_chain.h};
        views[e].subImage.imageArrayIndex = (uint32_t)e;
    }
    *layer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer->layerFlags = 0;   // opaque: the screen itself; black round it is the eye layer's clear
    layer->space = space;
    layer->viewCount = 2;
    layer->views = views;

    if (++s_xrFrames % 180 == 1) {
        LOGI("[vr-cinema] cutscene #%u game_frames=%u layer=projection %ux%u yaw=%.1f sep=%.1fmm far=%.1fmm P00=%.3f "
             "fov L(%.3f %.3f %.3f %.3f) R(%.3f %.3f %.3f %.3f) ipd=%.1fmm",
             s_cutscenes, s_gameFrames, s_chain.w, s_chain.h, s_screen.yaw * 57.29578f, 2000.0f * s_halfSepM,
             1000.0f * gevrCinemaFarParallax(s_lastP00, s_halfSepM, GEVR_CINEMA_DIST_M, s_screen.width), s_lastP00,
             s_lastFov[0][0], s_lastFov[0][1], s_lastFov[0][2], s_lastFov[0][3], s_lastFov[1][0], s_lastFov[1][1],
             s_lastFov[1][2], s_lastFov[1][3], s_ipd * 1000.0f);
    }
    return true;
}

/* ------------------------------------------------------------------------------- the cover */

static bool fade_ensure(void)
{
    if (s_fadeSc != XR_NULL_HANDLE) {
        return true;
    }
    if (g_vrState.session == XR_NULL_HANDLE) {
        return false;
    }
    XrSwapchainCreateInfo info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format      = gevrVrQuadSwapchainFormat();
    info.sampleCount = 1;
    info.width       = GEVR_FADE_TEX;
    info.height      = GEVR_FADE_TEX;
    info.faceCount   = 1;
    info.arraySize   = 1;
    info.mipCount    = 1;
    if (XR_FAILED(xrCreateSwapchain(g_vrState.session, &info, &s_fadeSc))) {
        LOGE("[vr] cinema 3D: the fade's swapchain failed: transitions cut without a fade");
        s_fadeSc = XR_NULL_HANDLE;
        return false;
    }
    uint32_t n = 0;
    xrEnumerateSwapchainImages(s_fadeSc, 0, &n, nullptr);
    s_fadeImages.resize(n, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
    xrEnumerateSwapchainImages(s_fadeSc, n, &n, reinterpret_cast<XrSwapchainImageBaseHeader *>(s_fadeImages.data()));
    s_fadeWritten = -1.0f;
    return true;
}

static bool fade_write(float alpha)
{
    uint32_t idx = 0;
    XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(xrAcquireSwapchainImage(s_fadeSc, &acquireInfo, &idx))) {
        return false;
    }
    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(s_fadeSc, &waitInfo);

    GLint prevFbo = 0;
    GLfloat prevClear[4];
    GLboolean prevMask[4];
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClear);
    glGetBooleanv(GL_COLOR_WRITEMASK, prevMask);
    static GLuint fbo = 0;
    if (!fbo) glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_fadeImages[idx].image, 0);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, alpha);   // black: premultiplied or not, the same
    glClear(GL_COLOR_BUFFER_BIT);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    glClearColor(prevClear[0], prevClear[1], prevClear[2], prevClear[3]);
    glColorMask(prevMask[0], prevMask[1], prevMask[2], prevMask[3]);
    if (scissor) glEnable(GL_SCISSOR_TEST);

    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(s_fadeSc, &releaseInfo);
    s_fadeWritten = alpha;
    return true;
}

bool gevrCinemaFadeLayer(XrSpace viewSpace, long long displayTimeNs, XrCompositionLayerQuad *out)
{
    if (!s_fadeActive || viewSpace == XR_NULL_HANDLE) {
        return false;
    }
    const float t = (float)((double)(displayTimeNs - s_fadeStartNs) * 1e-9);
    const float alpha = gevrCinemaFadeAlpha(t);
    if (alpha <= 0.0f && t > 0.0f) {
        s_fadeActive = false;
        return false;
    }
    if (!fade_ensure()) {
        s_fadeActive = false;
        return false;
    }
    // a quantised alpha: the image is written only when it changes (every XR frame of the fade)
    const float a = roundf(alpha * 255.0f) / 255.0f;
    if (a != s_fadeWritten && !fade_write(a)) {
        return false;
    }
    *out = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    out->layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    out->space = viewSpace;
    out->eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    out->subImage.swapchain = s_fadeSc;
    out->subImage.imageArrayIndex = 0;
    out->subImage.imageRect.offset = {0, 0};
    out->subImage.imageRect.extent = {GEVR_FADE_TEX, GEVR_FADE_TEX};
    out->pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    out->pose.position = {0.0f, 0.0f, -0.5f};
    out->size = {4.0f, 4.0f};   // 152 degrees at 0.5 m: the whole view
    return true;
}

extern "C" void gevrCinemaDestroy(void)
{
    if (s_chain.sc != XR_NULL_HANDLE) {
        xrDestroySwapchain(s_chain.sc);
    }
    s_chain = CinemaChain();
    if (s_fadeSc != XR_NULL_HANDLE) {
        xrDestroySwapchain(s_fadeSc);
        s_fadeSc = XR_NULL_HANDLE;
    }
    s_fadeImages.clear();
    s_fadeWritten = -1.0f;
    s_fadeActive = false;
    band_release();
    s_placed = false;
}

#endif /* GEVR_STEAM_FRAME */
