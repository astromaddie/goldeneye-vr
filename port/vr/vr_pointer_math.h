/*
 * The menu pointer as compositor quads (Steam Frame build; standard.md section 3, lessons 80 and 83).
 *
 * Round-3 wear (2026-10-05, 3c7aa52): on the title screen and in the menus the laser was drawn
 * behind the floating screen, although its layer (a projection layer of its own, 90a5b84) was
 * submitted after every other layer. SteamVR on the Frame does not keep a later projection layer
 * over an earlier quad: the fourth wear's beam in the eye layer hid behind the panels the same way,
 * and the GTA ports found SteamVR depth-composites quads that intersect (lesson 83). So the pointer
 * is now two quads, submitted last, that never reach the surface they point at: the beam, a strip
 * along the ray turned to face the head, ending GEVR_PTR_BEAM_GAP_M in front of the surface; the
 * dot, a disc laid flat on the surface (its face along the surface's normal), lifted
 * GEVR_PTR_DOT_LIFT_M toward the viewer. Whatever SteamVR does with layer order or depth, both are
 * nearer than the panel at every point.
 *
 * Vectors are 3 floats in one space (play space). Pure C, no state: the host test
 * (tools/steam_frame/tests/test_vr_pointer.c) runs these same functions.
 */
#ifndef VR_POINTER_MATH_H
#define VR_POINTER_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_PTR_BEAM_GAP_M  0.02f   /* the beam's end, this far in front of the surface (along its normal) */
#define GEVR_PTR_DOT_LIFT_M  0.01f   /* the dot's plane, this far in front of the surface */

typedef struct GevrPtrQuad {
    float pos[3];      /* centre */
    float quat[4];     /* x y z w: the quad's +X, +Y and +Z (its face's normal, toward the viewer) */
    float size[2];     /* width (along +X), height (along +Y), m */
} GevrPtrQuad;

/*
 * The beam and the dot for a ray from o that hits a surface at h, whose normal there is n (either
 * sign: it is turned toward the head), seen from head. The beam: +Y from o toward h (its image's
 * rows run from the hand to the hit), half width beamHalf; the dot: radius dotRadius, +Y the
 * world's up laid onto the surface (worldUp, unit). Returns bit 1 when the beam is drawn (the hand is
 * far enough in front of the surface), bit 2 for the dot (the head in front of the surface).
 */
int gevrPointerQuads(const float o[3], const float h[3], const float n[3], const float head[3],
                     const float worldUp[3], float beamHalf, float dotRadius,
                     GevrPtrQuad *beam, GevrPtrQuad *dot);

/* A quad's four corners (for the test and the dump): +X+Y, -X+Y, -X-Y, +X-Y. */
void gevrPointerQuadCorners(const GevrPtrQuad *q, float corners[4][3]);

#ifdef __cplusplus
}
#endif

#endif
