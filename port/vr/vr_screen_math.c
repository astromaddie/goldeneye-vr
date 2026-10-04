/*
 * The paused watch's screen placement (see vr_screen_math.h).
 *
 * The game links its own atan2f, sinf and cosf into this library (src/game, N64 ranges), and
 * they replace libm's for the VR code too: only the double versions are used here.
 */
#include <math.h>

#include "vr_screen_math.h"

void gevrScreenPlaceAhead(const float head[3], const float headQuat[4], float dist, float width,
                          float drop, int curved, GevrScreenPlace *out)
{
    const double x = headQuat[0], y = headQuat[1], z = headQuat[2], w = headQuat[3];
    /* the head's -Z (where it looks) and +Y, rotated into play space */
    double fx = -2.0 * (x * z + w * y);
    double fy = -2.0 * (y * z - w * x);
    double fz = -(1.0 - 2.0 * (x * x + y * y));
    double len = sqrt(fx * fx + fz * fz);
    double yaw, half;

    if (len < 0.05) {
        /* straight up or down: the top of the head points where the face would */
        const double ux = 2.0 * (x * y - w * z);
        const double uz = 2.0 * (y * z + w * x);
        const double s = fy < 0.0 ? 1.0 : -1.0;
        fx = ux * s;
        fz = uz * s;
        len = sqrt(fx * fx + fz * fz);
        if (len < 1e-6) {
            fx = 0.0;
            fz = -1.0;
            len = 1.0;
        }
    }
    fx /= len;
    fz /= len;

    out->pos[0] = head[0] + (float)fx * dist;
    out->pos[1] = head[1] - drop;
    out->pos[2] = head[2] + (float)fz * dist;
    /* a quad shows its +Z face: its -Z runs along the view */
    yaw = atan2(-fx, -fz);
    half = yaw * 0.5;
    out->yaw = (float)yaw;
    out->quat[0] = 0.0f;
    out->quat[1] = (float)sin(half);
    out->quat[2] = 0.0f;
    out->quat[3] = (float)cos(half);
    out->dist = dist;
    out->width = width;
    out->curved = curved ? 1 : 0;
    out->fovDeg = (float)((curved ? (double)width / dist : 2.0 * atan((double)width / (2.0 * dist)))
                          * 180.0 / 3.14159265358979323846);
}

int gevrWatchScreenStep(GevrWatchScreenState *s, int watchUp, int recentre)
{
    int place = 0;

    if (watchUp && (!s->up || recentre)) {
        place = 1;
        s->placements++;
    }
    s->up = watchUp ? 1 : 0;
    return place;
}
