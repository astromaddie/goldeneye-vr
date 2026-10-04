/*
 * Upright, camera-facing quads (standard.md section 4: no roll).
 *
 * The stereo game camera takes the head's whole orientation, roll included, so anything the game
 * builds from the camera's own right and up axes - its billboard sprites (explosions, smoke, hit
 * sparks), the 3D sight, the online name tags - and the HUD quads built in view space turned with
 * a tilted head (GoldenEye wear, 2026-10-04). A quad that faces the viewer and keeps world up has
 * its right axis horizontal (it turns about world up) and its up axis as close to world up as the
 * face allows (it still tilts with the view's pitch, so it faces the eye from above or below).
 *
 * Vectors are 3 floats in any one space; worldUp is world up in that same space (in view space,
 * the world's +Y turned by the camera). Pure C, no state: the host test
 * (tools/steam_frame/tests/test_vr_upright.c) runs these same functions.
 */
#ifndef VR_UPRIGHT_MATH_H
#define VR_UPRIGHT_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The basis of a quad whose face looks along `toViewer` (its normal, pointing at the viewer):
 * back = toViewer normalised, right = worldUp x back normalised (horizontal), up = back x right.
 * When the face looks straight up or down (worldUp nearly along it) the right axis is
 * `fallbackRight` made square to the face (the camera's own right there, so the quad does not
 * spin). Returns 0 if that fallback was used, 1 otherwise.
 */
int gevrUprightBasis(const float toViewer[3], const float worldUp[3], const float fallbackRight[3],
                     float right[3], float up[3], float back[3]);

/*
 * A camera's view-to-world rotation rows (row 0 its right, row 1 its up, row 2 its back, each of
 * any length, as the game's Mtxf keeps them, scaled) made roll-free: row 2 is kept, rows 0 and 1
 * are replaced by gevrUprightBasis's right and up for a face toward the camera (+row 2), keeping
 * their original lengths. World up is +Y. rows[3][3] is changed in place.
 */
void gevrUprightCameraRows(float rows[3][3]);

#ifdef __cplusplus
}
#endif

#endif
