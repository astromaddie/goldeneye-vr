/*
 * The scope's lens (issue #40), the Steam Frame build's maths. Pure C, no state: the host test
 * (tools/steam_frame/tests/test_vr_scope.c) runs these same functions, and the lens shader in
 * vr_openxr.cpp is gevrScopeDiscTexcoord in GLSL.
 *
 * History. Fourth wear (2026-10-04): a lens quad near the eyes, turned toward and pushed out along
 * the head's centre ray to dodge the compositor's near plane, slid off the aiming eye's line of
 * sight. 4811c1e then made the scope "optics": a collimated image on the gun's axis, each eye
 * looking through the eyepiece into it. Worn on 2026-10-05 that slid the picture out of the
 * eyepiece and away from the aim: through the eyepiece an eye saw the part of the image along its
 * own line through it, which only holds the aim point when that line is the gun's axis. Held in a
 * hand, the eye-to-eyepiece line is never quite the barrel's (journal: the right eye's window 0.2,
 * 11 degrees, off the image's centre), so the red sight sat at or past the window's edge.
 *
 * Now (round 3) the simplest reliable design: the zoomed picture is rendered along the shot ray and
 * drawn on the eyepiece's face, a disc square to the gun's axis, filling it, its centre (the aim
 * point and the red sight) at the disc's centre and its up the gun's up. Both eyes see that one disc
 * where the rifle model has its eyepiece; per eye it is a projection-layer view with that eye's own
 * pose (no near plane, nothing turned toward the head). Each texel of an eye's view is a ray from
 * the eye; where it meets the eyepiece's plane inside the circle it shows the picture at that point
 * of the disc. The magnification is the N64's over the disc's angular size from the aiming eye.
 *
 * Frames: "eye space" is the eye's own (x right, y up, -z ahead, metres); the caller turns the lens
 * and the gun's axes into it.
 */
#ifndef VR_SCOPE_MATH_H
#define VR_SCOPE_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_SCOPE_DISC_MIN_Z_M 0.005f   /* every point of the disc at least this far ahead of the eye */
#define GEVR_SCOPE_DISC_PAD     0.04f    /* the view's box past the disc's outline, a fraction of its size */
#define GEVR_SCOPE_DISC_STEPS   48       /* the outline sampled at this many points for the box */

typedef struct GevrScopeDisc {
    int visible;        /* the eye is behind the eyepiece (sees its face) and the disc is ahead of it */
    float lens[3];      /* the eyepiece's centre, eye space */
    float right[3];     /* the gun's right, up and axis (where it points: the gun's -back), eye space */
    float up[3];
    float axis[3];
    float radius;       /* the eyepiece's radius, m */
    float box[4];       /* the view's tangents round the disc: left, right, up, down (atanf = XrFovf) */
} GevrScopeDisc;

/* One eye's view of the eyepiece's disc. All vectors in that eye's space (unit axes). */
void gevrScopeDisc(const float lens[3], float radius, const float right[3], const float up[3],
                   const float axis[3], GevrScopeDisc *out);

/*
 * A view basis for an eye looking at the eyepiece: z = from the eyepiece's centre back to the eye
 * (the view looks along -z, straight at it), x level with the eye's own x-z plane (the eye's up kept
 * as far as it can be), y = z x x. Each eye's scope view uses its own position with this turn, so
 * its box is centred on the disc (the compositor is never handed a view far off its own axis).
 * In and out in eye space; the caller turns the lens and the gun's axes into this basis (dot with
 * x, y, z) before gevrScopeDisc.
 */
void gevrScopeViewBasis(const float lens[3], float x[3], float y[3], float z[3]);

/*
 * The eye looking through the scope: of the eyes that see the disc, the one nearest the eyepiece
 * (the eye it is held up to); -1 when neither sees it. cosOut (may be NULL): the cosine of the
 * angle between that eye's line to the eyepiece's centre and the barrel.
 */
int gevrScopeAimEye(const GevrScopeDisc eyes[2], float *cosOut);

/* The disc's angular radius (tangent) from an eye: what the N64's magnification is applied over. */
float gevrScopeDiscTan(const GevrScopeDisc *d);

/*
 * One texel of an eye's view: tan = the ray's tangents (x right, y up) in eye space. Returns 0 off
 * the disc (transparent), 1 on it, with tex = the scope picture's texture coordinate (0..1, x the
 * gun's right, y its up, 0.5 0.5 the aim point) and rim = the distance from the disc's centre over
 * its radius (0..1).
 */
int gevrScopeDiscTexcoord(const GevrScopeDisc *d, const float tan[2], float tex[2], float *rim);

/*
 * The scope camera (bondview2.c gevrScopeBeginHand): on the shot's line at origin, looking along
 * dir, with the gun's up as its up (made square to dir), fovDeg across, near/far in the same units.
 * vp: camera space to the scope's clip space, a column-major GL matrix (rows g right, g up, the
 * depth row, -forward). Returns 0 when dir and up are parallel.
 */
int gevrScopeCameraMatrix(const float origin[3], const float dir[3], const float up[3], float fovDeg,
                          float nearZ, float farZ, float vp[16]);

#ifdef __cplusplus
}
#endif

#endif
