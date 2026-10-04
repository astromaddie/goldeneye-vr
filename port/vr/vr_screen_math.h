/*
 * Where the paused watch's screen hangs (Steam Frame build, watch option D "Better Big Screen",
 * chosen 2026-10-04; vr_openxr.cpp gevrVrWatchScreen).
 *
 * The pause shows the game's own zoomed watch image on a curved screen that is world-locked, not
 * pinned to the view: straight ahead at the head's yaw when the watch comes up (yaw only, so a
 * head tilted down or rolled does not tilt it), its centre a little below eye height, 1.4 m wide
 * at 1.5 m. It stays there until the watch closes, and is placed again on the next pause and on
 * a recentre (standard.md section 3).
 *
 * Quaternions are {x, y, z, w}; OpenXR play space (+Y up, -Z ahead). Pure C, no state but the
 * small struct below: the host test (tools/steam_frame/tests/test_vr_screen.c) runs these same
 * functions.
 */
#ifndef VR_SCREEN_MATH_H
#define VR_SCREEN_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_WATCH_SCREEN_DIST_M   1.5f    /* from the head to the screen's centre (a curved one's radius) */
#define GEVR_WATCH_SCREEN_WIDTH_M  1.4f    /* flat: the chord; curved: the arc */
#define GEVR_WATCH_SCREEN_DROP_M   0.10f   /* its centre below eye height (about 4 degrees) */

typedef struct GevrScreenPlace {
    float pos[3];     /* the centre of the screen's surface, play space */
    float quat[4];    /* yaw only; the screen's face (+Z) points back at the head */
    float yaw;        /* radians about +Y, as in vr_openxr.cpp g_screenYaw */
    float dist;       /* metres; a curved screen's radius */
    float width;      /* metres */
    float fovDeg;     /* the horizontal angle it spans from the head: flat 2 atan(w / 2d), curved w / d */
    int   curved;
} GevrScreenPlace;

/*
 * A screen `dist` ahead of `head` (the eyes' midpoint) along the head's yaw, `drop` below it.
 * Looking straight up or down, the yaw is taken from the head's up vector instead.
 */
void gevrScreenPlaceAhead(const float head[3], const float headQuat[4], float dist, float width,
                          float drop, int curved, GevrScreenPlace *out);

/* When the watch's screen is (re)placed: as it comes up, and on a recentre while it is up. */
typedef struct GevrWatchScreenState {
    int up;
    unsigned placements;   /* how many times it was placed (logs and tests) */
} GevrWatchScreenState;

/* Once a game frame (recentre 0), and from a recentre (recentre 1, watchUp = s->up).
 * Returns 1 when the screen is to be placed now. */
int gevrWatchScreenStep(GevrWatchScreenState *s, int watchUp, int recentre);


/*
 * The Q-Branch terminal (watch option C, chosen 2026-10-04 in place of D; vr_watch_terminal.cpp).
 *
 * The watch beams out a flat panel, world-locked: 0.9 m ahead of the head along its yaw, its centre
 * 15 cm below eye height, pitched so its face looks straight back at the eyes (the bottom toward
 * the viewer), 60 x 38 cm. It is placed on each open and on a recentre (pivot = the head then, so a
 * seated player's panel comes up at a seated height), and it lazily re-centres - a smooth glide,
 * not a snap - only once the head has turned more than 50 degrees away from it, until it is back
 * within 2 degrees. The VR settings opened from the watch dock as a second panel to its right,
 * hinged 3 cm off its right edge and turned 20 degrees in toward the player (a triptych).
 *
 * SteamVR on the Frame offers no cylinder layer, so both are flat quad layers.
 */
#define GEVR_TERM_DIST_M          0.90f   /* horizontal, from the head to the panel's centre */
#define GEVR_TERM_DROP_M          0.15f   /* its centre below eye height */
#define GEVR_TERM_WIDTH_M         0.60f
#define GEVR_TERM_HEIGHT_M        0.38f
#define GEVR_TERM_FOLLOW_DEG      50.0f   /* the head this far from it: it starts to re-centre */
#define GEVR_TERM_SETTLE_DEG      2.0f    /* ... and stops this close */
#define GEVR_TERM_FOLLOW_TAU_S    0.20f   /* the glide's time constant */
#define GEVR_TERM_DOCK_DEG        20.0f   /* the VR settings panel, turned in toward the player */
#define GEVR_TERM_DOCK_GAP_M      0.03f
#define GEVR_TERM_DOCK_HEIGHT_M   0.48f   /* its 4:3 page 64 cm wide: ImGui text about 0.85 degrees */
#define GEVR_TERM_OPEN_S          0.22f   /* the beam out of the watch */
#define GEVR_TERM_CLOSE_S         0.18f   /* ... and back into it */
#define GEVR_TERM_BEAM_MIN        0.06f   /* the panel's size as it leaves the watch, of its full size */

typedef struct GevrTermPanel {
    float pivot[3];       /* the head (eyes' midpoint) it hangs from; its y fixes the panel's height */
    float yaw;            /* radians about +Y, as GevrScreenPlace.yaw: the panel lies along it */
    int following;        /* lazily re-centring */
    unsigned placements;  /* logs and tests */
} GevrTermPanel;

/* The head's yaw (radians, GevrScreenPlace convention); straight up or down, from its up vector. */
float gevrHeadYaw(const float headQuat[4]);

/* (Re)place the panel in front of `head` (eyes' midpoint) at its yaw: on open and on a recentre. */
void gevrTermPlace(GevrTermPanel *p, const float head[3], const float headQuat[4]);

/* The panel's centre and orientation (yaw, then pitched to face the pivot's eyes); size in metres. */
void gevrTermPose(const GevrTermPanel *p, float pos[3], float quat[4]);

/* The docked VR settings panel for a page of this aspect (w / h): centre, orientation, size. */
void gevrTermDockPose(const GevrTermPanel *p, float aspect, float pos[3], float quat[4], float size[2]);

/* Once a display frame while it is up (dt seconds, display time): the lazy re-centre. The pivot's
 * height stays; its x/z and the yaw glide to the head's. Returns 1 while it moves. */
int gevrTermFollow(GevrTermPanel *p, const float head[3], const float headQuat[4], float dt);

/* The beam: ease-out of the open fraction s (0 = in the watch, 1 = in place). */
float gevrTermEase(float s);

/*
 * A ray (origin o, unit direction d) against a quad (centre pos, orientation quat, w x h metres,
 * showing its +Z face). Hits only the front, ahead of o. u runs left to right, v top to bottom,
 * both 0..1 on the quad. Returns 1 on a hit.
 */
int gevrQuadHit(const float pos[3], const float quat[4], float w, float h, const float o[3],
                const float d[3], float *u, float *v);
/* The point at u, v on that quad (play space). */
void gevrQuadPoint(const float pos[3], const float quat[4], float w, float h, float u, float v, float out[3]);

/*
 * The page's crop for the terminal's texture (gfx_pc.cpp): the watch face's rectangle as the game
 * drew it (native pixels x0, y0, x1, y1, y down), grown by `margin` of its larger side all round
 * and then widened (or heightened) about its centre to the texture's aspect (w / h). A face smaller
 * than a quarter of the screen either way is not one: the whole screen is used instead, and 0 is
 * returned. out = x, y, w, h (native pixels).
 */
int gevrTermCrop(const float box[4], float screenW, float screenH, float aspect, float margin, float out[4]);

#ifdef __cplusplus
}
#endif

#endif
