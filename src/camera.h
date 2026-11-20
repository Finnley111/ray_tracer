#pragma once
#include "vec.h"
#include <cmath>
#include <numbers>

// Pinhole camera. ray(u, v): u in [0,1] left->right, v in [0,1] top->bottom.
struct Camera {
    Vec3 origin, top_left, horiz, vert, fwd;
    float film_area;  // area of the image plane at distance 1 (pinhole importance)

    Camera(Vec3 from, Vec3 at, Vec3 up, float vfov_deg, float aspect) : origin(from) {
        float hh = std::tan(vfov_deg * std::numbers::pi_v<float> / 360.0f);  // half-height at distance 1
        Vec3 w = normalize(from - at), u = normalize(cross(up, w)), v = cross(w, u);
        horiz = u * (2 * hh * aspect);
        vert = v * (2 * hh);
        top_left = from - w - horiz / 2 + vert / 2;
        fwd = -w;
        film_area = length(horiz) * length(vert);
    }

    // Image position (u, v as for ray()) of world point x, and cos of its angle to the
    // view axis. False if x is behind the camera or outside the frame.
    bool project(Vec3 x, float& u, float& v, float& cos_theta) const {
        Vec3 d = x - origin;
        float z = dot(d, fwd);
        if (z <= 0) return false;
        Vec3 rel = d / z - (top_left - origin);
        u = dot(rel, horiz) / dot(horiz, horiz);
        v = -dot(rel, vert) / dot(vert, vert);
        cos_theta = z / length(d);
        return u >= 0 && u < 1 && v >= 0 && v < 1;
    }

    Ray ray(float u, float v) const { return {origin, normalize(top_left + horiz * u - vert * v - origin)}; }
};
