#pragma once
#include "vec.h"
#include <cmath>
#include <numbers>

// Pinhole camera. ray(u, v): u in [0,1] left->right, v in [0,1] top->bottom.
struct Camera {
    Vec3 origin, top_left, horiz, vert;

    Camera(Vec3 from, Vec3 at, Vec3 up, float vfov_deg, float aspect) : origin(from) {
        float hh = std::tan(vfov_deg * std::numbers::pi_v<float> / 360.0f);  // half-height at distance 1
        Vec3 w = normalize(from - at), u = normalize(cross(up, w)), v = cross(w, u);
        horiz = u * (2 * hh * aspect);
        vert = v * (2 * hh);
        top_left = from - w - horiz / 2 + vert / 2;
    }

    Ray ray(float u, float v) const { return {origin, normalize(top_left + horiz * u - vert * v - origin)}; }
};
