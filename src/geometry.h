#pragma once
#include "vec.h"
#include <cmath>
#include <utility>
#include <vector>

struct Hit {
    float t;
    Vec3 p;
    Vec3 n;      // unit normal, always facing against the incoming ray
    bool front;  // true if the ray hit the outside of the surface
    int mat;     // index into the scene's material list
};

// Orient the stored normal against the ray; remember which side we hit.
inline void set_normal(Hit& h, const Ray& r, Vec3 outward) {
    h.front = dot(r.d, outward) < 0;
    h.n = h.front ? outward : -outward;
}

struct Sphere {
    Vec3 c;
    float r;
    int mat;

    bool intersect(const Ray& ray, float tmin, float tmax, Hit& h) const {
        Vec3 oc = ray.o - c;
        float a = dot(ray.d, ray.d), hb = dot(oc, ray.d), cc = dot(oc, oc) - r * r;
        float disc = hb * hb - a * cc;
        if (disc < 0) return false;
        float sq = std::sqrt(disc), t = (-hb - sq) / a;
        if (t <= tmin || t >= tmax) {
            t = (-hb + sq) / a;
            if (t <= tmin || t >= tmax) return false;
        }
        h.t = t;
        h.p = ray.at(t);
        set_normal(h, ray, (h.p - c) / r);
        h.mat = mat;
        return true;
    }
};

// Parallelogram with corner q and edges u, v. Outward normal = normalize(u x v).
struct Quad {
    Vec3 q, u, v;
    int mat;

    bool intersect(const Ray& ray, float tmin, float tmax, Hit& h) const {
        Vec3 n = cross(u, v);
        float denom = dot(n, ray.d);
        if (std::fabs(denom) < 1e-8f) return false;
        float t = dot(n, q - ray.o) / denom;
        if (t <= tmin || t >= tmax) return false;
        // Planar coordinates of the hit point in the (u, v) basis.
        Vec3 w = n / dot(n, n), rel = ray.at(t) - q;
        float a = dot(w, cross(rel, v)), b = dot(w, cross(u, rel));
        if (a < 0 || a > 1 || b < 0 || b > 1) return false;
        h.t = t;
        h.p = ray.at(t);
        set_normal(h, ray, normalize(n));
        h.mat = mat;
        return true;
    }
};

// Triangle, Moller-Trumbore. Outward normal = normalize((b-a) x (c-a)), so winding matters
// for dielectrics (it decides entering vs exiting).
struct Triangle {
    Vec3 a, b, c;
    int mat;

    bool intersect(const Ray& ray, float tmin, float tmax, Hit& h) const {
        Vec3 e1 = b - a, e2 = c - a, p = cross(ray.d, e2);
        float det = dot(e1, p);
        if (std::fabs(det) < 1e-12f) return false;
        float inv = 1 / det;
        Vec3 s = ray.o - a;
        float u = dot(s, p) * inv;
        if (u < 0 || u > 1) return false;
        Vec3 q = cross(s, e1);
        float v = dot(ray.d, q) * inv;
        if (v < 0 || u + v > 1) return false;
        float t = dot(e2, q) * inv;
        if (t <= tmin || t >= tmax) return false;
        h.t = t;
        h.p = ray.at(t);
        set_normal(h, ray, normalize(cross(e1, e2)));
        h.mat = mat;
        return true;
    }
};

// Fix winding so every triangle of a closed convex mesh faces away from its centroid.
inline void orient_convex(std::vector<Triangle>& tris) {
    Vec3 c;
    for (const auto& t : tris) c += (t.a + t.b + t.c) / (3.0f * tris.size());
    for (auto& t : tris)
        if (dot(cross(t.b - t.a, t.c - t.a), (t.a + t.b + t.c) / 3.0f - c) < 0) std::swap(t.b, t.c);
}

// Parallelepiped from corner p and edge vectors a, b, c, as six quads.
inline void add_box(std::vector<Quad>& out, Vec3 p, Vec3 a, Vec3 b, Vec3 c, int mat) {
    out.insert(out.end(), {{p, a, b, mat}, {p + c, a, b, mat}, {p, b, c, mat},
                           {p + a, b, c, mat}, {p, c, a, mat}, {p + b, c, a, mat}});
}

// ponytail: linear scan over every shape; BVH in RT-013.
struct Scene {
    std::vector<Sphere> spheres;
    std::vector<Quad> quads;
    std::vector<Triangle> tris;

    bool intersect(const Ray& r, float tmin, float tmax, Hit& h) const {
        bool any = false;
        for (const auto& s : spheres) if (s.intersect(r, tmin, tmax, h)) { any = true; tmax = h.t; }
        for (const auto& q : quads) if (q.intersect(r, tmin, tmax, h)) { any = true; tmax = h.t; }
        for (const auto& t : tris) if (t.intersect(r, tmin, tmax, h)) { any = true; tmax = h.t; }
        return any;
    }
};
