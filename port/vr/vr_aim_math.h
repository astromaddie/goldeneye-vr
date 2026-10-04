/*
 * The gun's aim from a controller (bondview2.c gevrGripAxesRaw / gevrGunAxesRaw, vr_input.cpp).
 *
 * The game builds the gun from the grip pose with a fixed axis map, measured on Quest Touch:
 * the holder's right is grip +X, up is grip -Z and back is grip +Y, so the barrel is grip -Y.
 * On the Steam Frame controller that barrel points 30-45 degrees below where the hand points.
 * The runtime's aim pose (-Z forward, +Y up, +X right) is the pointing ray, so the Frame build
 * measures, per controller, the rigid turn from its grip pose to its aim pose and gives the gun
 * the grip turned by it:
 *
 *   off = conj(grip) * aim * Rx(+90)     gun = grip * off
 *
 * Rx(+90) takes the aim pose's axes onto the grip map's: +X stays +X, aim +Y becomes the map's
 * up (-Z) and aim -Z its barrel (-Y). So gun's barrel = aim -Z, up = aim +Y, right = aim +X.
 * Both poses come from the same rigid controller, so `off` is constant; it is relative to the
 * grip, so it holds in play space and in view space alike. With `off` the identity the gun is the
 * grip, exactly as before.
 *
 * Quaternions are {x, y, z, w}, Hamilton products, rotating column vectors. Pure C, no state:
 * the host test (tools/steam_frame/tests/test_vr_aim.c) runs these same functions.
 */
#ifndef VR_AIM_MATH_H
#define VR_AIM_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

/* out = a * b (out may alias neither) */
void gevrQuatMul(const float a[4], const float b[4], float out[4]);

/* The grip-local turn from a controller's grip pose to its aim pose, in the grip map's terms
 * (see above), normalised and with w >= 0. Both orientations in the same space. */
void gevrAimOffsetFromPoses(const float grip[4], const float aim[4], float off[4]);

/* The gun's orientation: grip * off, normalised. */
void gevrGunQuat(const float grip[4], const float off[4], float out[4]);

/* The grip map: the holder's right (+X), up (-Z) and back (+Y) of an orientation, as the columns
 * of its rotation matrix. The barrel is -back. */
void gevrGripAxesFromQuat(const float q[4], float right[3], float up[3], float back[3]);

/* The angle of a unit quaternion's turn, degrees (0..180). */
float gevrQuatAngleDeg(const float q[4]);

#ifdef __cplusplus
}
#endif

#endif
