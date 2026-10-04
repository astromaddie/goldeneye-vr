/*
 * The scope's lens as optics (issue #40; GoldenEye wear 2026-10-04: "the scope image shifts to the
 * right the closer the rifle comes to the eyes").
 *
 * A riflescope's image is collimated: what it shows sits at infinity along its optical axis, so
 * the target and the sight in it are where the gun points, from any eye and at any eye relief.
 * The eyepiece is only the window it is seen through. So the lens is no longer a picture on a quad
 * at the eyepiece (whose place, seen from an eye, moves with the distance, and which the old near-
 * plane fix pushed out along the head's ray, off the aiming eye's line of sight). It is a
 * projection layer: per eye, the scope's image laid out in the tangent plane of the scope's axis
 * (x along the gun's right, y its up), and cut to the eyepiece's circle as that eye sees it.
 *
 * In that tangent space an eye at `eye` sees the eyepiece (centre c, radius R, its face square to
 * the axis) as the circle centred on ((c - eye).right, (c - eye).up) / depth with radius R / depth,
 * depth = (c - eye).axis: exact, the projection of a circle onto a parallel plane. Both eyes look
 * into the same image (same tangent, same picture: no mismatched copy); each sees the part its own
 * line of sight through the eyepiece picks out. The image covers a field of half-width `field`
 * (tangent) round the axis; an eye whose window falls outside it sees the dark glass there, as off
 * a real scope's exit pupil. The field covers both eyes' windows while that stays under a cap
 * (resolution: the image is a fixed 512 texels), and always the nearest eye's whole window.
 * The magnification is unchanged: tangent t in the eye shows the world at t / M (the N64's zoom),
 * so bondview2.c renders the image over field / M.
 *
 * Vectors in one space (view space, metres). Pure C, no state: the host test
 * (tools/steam_frame/tests/test_vr_scope.c) runs these same functions; the lens copy shader in
 * vr_openxr.cpp is gevrScopeEyeTexcoord in GLSL.
 */
#ifndef VR_SCOPE_MATH_H
#define VR_SCOPE_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_SCOPE_EYE_MIN_DEPTH_M 0.015f   /* nearer than this along the axis (or behind it): not seen */
#define GEVR_SCOPE_FIELD_CAP 0.40f          /* shared field, tangent: ~0.7 texels a display pixel at 512 */
#define GEVR_SCOPE_FIELD_MAX 1.0f           /* never wider than 45 degrees off the axis */

typedef struct GevrScopeEyeView {
    int visible;        /* the eye is behind the eyepiece and sees its face */
    float center[2];    /* the eyepiece's circle in the axis's tangent plane (x right, y up) */
    float radius;       /* its radius, tangent */
    float depth;        /* the eyepiece's distance along the axis from the eye, m */
} GevrScopeEyeView;

/* One eye's view of the eyepiece. lens = its centre, radius R (m); right, up, axis = the scope's
 * basis (unit; axis is where it points, the gun's -back). */
void gevrScopeEyeAperture(const float lens[3], float radius, const float right[3], const float up[3],
                          const float axis[3], const float eye[3], GevrScopeEyeView *out);

/* The shared field's half-width (tangent) for these two eyes and the eye nearest the axis
 * (smallest |center|), -1 when neither sees the eyepiece (then 0 is returned). */
float gevrScopeField(const GevrScopeEyeView eyes[2], int *nearest);

/* The tangent rectangle a layer view covers for this eye: the eyepiece's circle's box,
 * {left, right, up, down} (atanf of them is the XrFovf). */
void gevrScopeEyeFovTan(const GevrScopeEyeView *v, float fovTan[4]);

/*
 * The copy, per texel of an eye's layer image: q in [-1, 1]^2 across that eye's box. Returns
 * 0 outside the eyepiece (transparent), 1 inside it within the field (tex = the scope image's
 * texture coordinate, 0..1), 2 inside it past the field (the dark glass).
 */
int gevrScopeEyeTexcoord(const GevrScopeEyeView *v, float field, const float q[2], float tex[2]);

#ifdef __cplusplus
}
#endif

#endif
