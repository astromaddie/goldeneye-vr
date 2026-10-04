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

#ifdef __cplusplus
}
#endif

#endif
