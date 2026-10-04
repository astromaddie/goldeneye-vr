/*
 * The 3D cinema for in-engine cutscenes (Steam Frame build; standard.md section 4 "Cutscenes",
 * the user 2026-10-04). Pure C, no state: the host test (tools/steam_frame/tests/test_vr_cinema.c)
 * runs these same functions.
 *
 * GoldenEye's scripted cameras (the mission intro flybys, the swirl into Bond, the death replay,
 * the mission-end cameras) are rendered from the game's camera twice, in one multiview pass: two
 * parallel cameras GEVR_CINEMA_SEP_M apart along the camera's right axis, each with an off-axis
 * window so both windows meet at a plane GEVR_CINEMA_DIST_M ahead (zero parallax). The picture is
 * shown on a world-locked screen GEVR_CINEMA_DIST_M ahead at the head's yaw when the cutscene
 * started, as a PROJECTION layer: per eye, pose = (the screen's orientation, that eye's position)
 * and fov = the screen rectangle as seen from that eye (atan2 of its corners in the screen's frame).
 * Per-eye quad layers (eyeVisibility) show one image to both eyes on SteamVR (lessons.md 45).
 *
 * The screen is the 4:3 picture with a black band under it for the bottom messages (lessons.md 72):
 * GEVR_CINEMA_WIDTH_M wide, the picture's height width / aspect, the band GEVR_CINEMA_BAND_FRAC of
 * the picture's height. The picture's centre is GEVR_CINEMA_DROP_M below eye height.
 *
 * Quaternions are {x, y, z, w}; OpenXR play space (+Y up, -Z ahead). Field of view as OpenXR's
 * XrFovf: angles in radians, left and down negative.
 */
#ifndef VR_CINEMA_MATH_H
#define VR_CINEMA_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_CINEMA_DIST_M     3.0f    /* head to screen, and the zero-parallax plane in the game (Halo: 3 m) */
#define GEVR_CINEMA_WIDTH_M    3.0f    /* the 4:3 picture: 3.0 x 2.25 m, 53 x 41 degrees (Halo's 16:9 3.6 m, same height class) */
#define GEVR_CINEMA_DROP_M     0.10f   /* the picture's centre below eye height */
#define GEVR_CINEMA_BAND_FRAC  0.20f   /* the subtitle band under the picture, of the picture's height */
#define GEVR_CINEMA_SEP_M      0.064f  /* the two cameras' separation (Halo, the standard's default) */
#define GEVR_CINEMA_FAR_MAX    0.75f   /* the far field's separation on the screen, at most this of the IPD */
#define GEVR_CINEMA_HOLD_S     0.25f   /* every change into or out of a cutscene: black this long ... */
#define GEVR_CINEMA_FADE_S     0.35f   /* ... then fades in over this (standard.md "Transitions") */

/* The screen as placed: the centre of the whole rectangle (picture and band), its yaw, its size. */
typedef struct GevrCinemaScreen {
    float centre[3];   /* play space: the centre of the rectangle, picture and band together */
    float quat[4];     /* yaw only; the face (+Z) points back at the head */
    float yaw;         /* radians about +Y */
    float dist;        /* metres from the head as placed */
    float width;       /* metres */
    float picH;        /* the picture's height, metres */
    float bandH;       /* the band's height, metres */
} GevrCinemaScreen;

/*
 * Place the screen ahead of `head` (the eyes' midpoint) along the head's yaw, for a picture of
 * `aspect` (width / height). Tilting or rolling the head does not tilt the screen.
 */
void gevrCinemaPlace(const float head[3], const float headQuat[4], float aspect, GevrCinemaScreen *out);

/* The same screen for a new picture aspect, keeping its place (the picture's centre stays put). */
void gevrCinemaSetAspect(GevrCinemaScreen *s, float aspect);

/*
 * One eye's projection view onto the screen: the field of view (left, right, up, down; radians)
 * through the screen rectangle from `eye` (play space), for a view whose orientation is the
 * screen's. Returns 0 when the eye is not at least 10 cm in front of the screen (no view).
 * `inFront` (optional) gets the eye's distance in front of the screen plane.
 */
int gevrCinemaEyeFov(const GevrCinemaScreen *s, const float eye[3], float fov[4], float *inFront);

/*
 * The multiview shader's per-eye offsets for the cinema pass (gfx_opengl.cpp, uVrFlat 2), in the
 * layout of gfx_opengl_set_eye_offsets: { ex, ey, 0, 0 } for the left eye, then the right.
 * The shader moves a world vertex x' = x - ex - ey * w (clip space, w = the depth in view units):
 *   ex = tx * P00 is the camera moved by tx = -/+ halfSep along its right axis,
 *   ey = -tx * P00 / converge shears its window so a point at depth `converge` lands where it did,
 * so the two images agree at the zero-parallax plane and part behind it (far field: 2 halfSep
 * P00 / converge in NDC). halfSep and converge in view units; P00 the game projection's [0][0].
 */
void gevrCinemaShear(float p00, float halfSep, float converge, float out[8]);

/*
 * The far field's separation on the screen, metres, for a picture `screenW` wide: the parallax a
 * point at infinity gets (both eyes together). It must stay under the viewer's IPD or the eyes
 * diverge; gevrCinemaSafeHalfSep keeps it at GEVR_CINEMA_FAR_MAX of it.
 */
float gevrCinemaFarParallax(float p00, float halfSepM, float convergeM, float screenW);

/* The half separation (metres) to use: `halfSepM`, or less if the far field would part by more
 * than GEVR_CINEMA_FAR_MAX x ipd on the screen (a zoomed camera: a large P00). */
float gevrCinemaSafeHalfSep(float p00, float halfSepM, float convergeM, float screenW, float ipdM);

/* The transition cover's opacity `t` seconds after a change: 1 for GEVR_CINEMA_HOLD_S, then a
 * linear fade to 0 over GEVR_CINEMA_FADE_S; 0 before (t < 0) and after. */
float gevrCinemaFadeAlpha(float t);

/*
 * The cutscene latch (lessons.md 71: through a latch, never a raw per-frame flag). `raw`: this
 * game frame shows a scripted camera; `stereo`: stereo play wants the view this frame. On at the
 * first raw frame; off when stereo play takes over, or after GEVR_CINEMA_RELEASE_FRAMES frames
 * without raw (the camera-mode hand-overs pass through CAMERAMODE_NONE for a frame).
 */
#define GEVR_CINEMA_RELEASE_FRAMES 12
typedef struct GevrCinemaLatch {
    int on;
    int quiet;          /* game frames since raw was last seen while on */
    unsigned starts;    /* how many times it latched on (logs and tests) */
} GevrCinemaLatch;
/* Returns the latch after this frame (0/1). */
int gevrCinemaLatchStep(GevrCinemaLatch *l, int raw, int stereo);

#ifdef __cplusplus
}
#endif

#endif
