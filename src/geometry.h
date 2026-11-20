#pragma once
#include "vec.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numbers>
#include <utility>
#include <vector>

struct Hit {
    float t;
    Vec3 p;
    Vec3 n;      // unit normal, always facing against the incoming ray
    bool front;  // true if the ray hit the outside of the surface
    int mat;     // index into the scene's material list
    int shape;   // 0 sphere, 1 quad, 2 triangle
    float area;  // surface area of the shape hit (light pdf for MIS)
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
        h.shape = 0;
        h.area = 4 * std::numbers::pi_v<float> * r * r;
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
        h.area = length(n);
        set_normal(h, ray, n / h.area);
        h.mat = mat;
        h.shape = 1;
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
        Vec3 n = cross(e1, e2);
        h.area = length(n) / 2;
        set_normal(h, ray, n / (2 * h.area));
        h.mat = mat;
        h.shape = 2;
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

struct AABB {
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    void grow(Vec3 p) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    void grow(const AABB& b) { grow(b.lo); grow(b.hi); }
    float area() const {
        Vec3 e = hi - lo;
        return e.x < 0 ? 0 : 2 * (e.x * e.y + e.y * e.z + e.z * e.x);
    }
    // Slab test. Comparisons are written so a NaN (0 * inf) leaves the interval unchanged.
    bool hit(const Ray& r, Vec3 inv, float tmin, float tmax) const {
        for (int i = 0; i < 3; ++i) {
            float t0 = (lo[i] - r.o[i]) * inv[i], t1 = (hi[i] - r.o[i]) * inv[i];
            if (inv[i] < 0) std::swap(t0, t1);
            tmin = t0 > tmin ? t0 : tmin;
            tmax = t1 < tmax ? t1 : tmax;
            if (tmax < tmin) return false;
        }
        return true;
    }
};

// All shapes, plus a binned-SAH BVH over them once build() has run. Without a BVH,
// intersect() scans every shape (the reference the tests compare against).
struct Scene {
    std::vector<Sphere> spheres;
    std::vector<Quad> quads;
    std::vector<Triangle> tris;

    struct Prim { int type, idx; AABB box; Vec3 c; };  // type 0 sphere, 1 quad, 2 triangle
    struct Node { AABB box; int start, count, right, axis; };  // leaf if count > 0
    std::vector<Prim> prims;
    std::vector<Node> nodes;

    // Lights for next-event estimation: every emissive sphere and quad (emissive
    // triangles are not sampled; MIS gives them full BSDF weight).
    struct Light { int shape, idx; };
    std::vector<Light> lights;
    bool use_nee = true;
    float clamp = 0;  // max luminance (Y) per sample, 0 = off; biased firefly removal

    template <class M>
    void collect_lights(const std::vector<M>& mats) {
        lights.clear();
        for (int i = 0; i < int(spheres.size()); ++i) if (mats[spheres[i].mat].emission_scale > 0) lights.push_back({0, i});
        for (int i = 0; i < int(quads.size()); ++i) if (mats[quads[i].mat].emission_scale > 0) lights.push_back({1, i});
    }

    // Uniform point on light l: position, unit normal, area, material.
    void sample_light(const Light& l, float r1, float r2, Vec3& p, Vec3& n, float& area, int& mat) const {
        if (l.shape == 0) {
            const Sphere& s = spheres[l.idx];
            float z = 1 - 2 * r1, rr = std::sqrt(std::max(0.0f, 1 - z * z)), phi = 2 * std::numbers::pi_v<float> * r2;
            n = {rr * std::cos(phi), rr * std::sin(phi), z};
            p = s.c + n * s.r;
            area = 4 * std::numbers::pi_v<float> * s.r * s.r;
            mat = s.mat;
        } else {
            const Quad& q = quads[l.idx];
            Vec3 c = cross(q.u, q.v);
            area = length(c);
            n = c / area;
            p = q.q + q.u * r1 + q.v * r2;
            mat = q.mat;
        }
    }

    bool built() const { return !nodes.empty(); }

    bool hit_prim(const Prim& p, const Ray& r, float tmin, float tmax, Hit& h) const {
        switch (p.type) {
            case 0: return spheres[p.idx].intersect(r, tmin, tmax, h);
            case 1: return quads[p.idx].intersect(r, tmin, tmax, h);
            default: return tris[p.idx].intersect(r, tmin, tmax, h);
        }
    }

    bool intersect_linear(const Ray& r, float tmin, float tmax, Hit& h) const {
        bool any = false;
        for (const auto& s : spheres) if (s.intersect(r, tmin, tmax, h)) { any = true; tmax = h.t; }
        for (const auto& q : quads) if (q.intersect(r, tmin, tmax, h)) { any = true; tmax = h.t; }
        for (const auto& t : tris) if (t.intersect(r, tmin, tmax, h)) { any = true; tmax = h.t; }
        return any;
    }

    bool intersect(const Ray& r, float tmin, float tmax, Hit& h) const {
        if (!built()) return intersect_linear(r, tmin, tmax, h);
        Vec3 inv{1 / r.d.x, 1 / r.d.y, 1 / r.d.z};
        int stack[64], sp = 0, n = 0;
        bool any = false;
        for (;;) {
            const Node& nd = nodes[n];
            if (nd.box.hit(r, inv, tmin, tmax)) {
                if (nd.count > 0) {
                    for (int i = nd.start; i < nd.start + nd.count; ++i)
                        if (hit_prim(prims[i], r, tmin, tmax, h)) { any = true; tmax = h.t; }
                } else {
                    // Visit the child nearer along the split axis first.
                    bool flip = r.d[nd.axis] < 0;
                    stack[sp++] = flip ? n + 1 : nd.right;
                    n = flip ? nd.right : n + 1;
                    continue;
                }
            }
            if (sp == 0) break;
            n = stack[--sp];
        }
        return any;
    }

    void build() {
        prims.clear();
        nodes.clear();
        auto add = [&](int type, int idx, std::initializer_list<Vec3> pts, float pad) {
            Prim p{type, idx, {}, {}};
            for (Vec3 q : pts) p.box.grow(q);
            p.box.lo = p.box.lo - Vec3{pad, pad, pad};  // flat quads/triangles get some thickness
            p.box.hi = p.box.hi + Vec3{pad, pad, pad};
            p.c = (p.box.lo + p.box.hi) * 0.5f;
            prims.push_back(p);
        };
        for (int i = 0; i < int(spheres.size()); ++i) {
            const auto& s = spheres[i];
            add(0, i, {s.c - Vec3{s.r, s.r, s.r}, s.c + Vec3{s.r, s.r, s.r}}, 0);
        }
        for (int i = 0; i < int(quads.size()); ++i) {
            const auto& q = quads[i];
            add(1, i, {q.q, q.q + q.u, q.q + q.v, q.q + q.u + q.v}, 1e-4f);
        }
        for (int i = 0; i < int(tris.size()); ++i) add(2, i, {tris[i].a, tris[i].b, tris[i].c}, 1e-4f);
        if (!prims.empty()) build_node(0, int(prims.size()));
    }

private:
    int build_node(int start, int end) {
        int idx = int(nodes.size());
        nodes.push_back({});
        AABB box, cbox;
        for (int i = start; i < end; ++i) { box.grow(prims[i].box); cbox.grow(prims[i].c); }
        nodes[idx].box = box;
        int n = end - start;
        Vec3 ext = cbox.hi - cbox.lo;
        int axis = ext.x > ext.y && ext.x > ext.z ? 0 : ext.y > ext.z ? 1 : 2;
        auto make_leaf = [&] { nodes[idx].start = start; nodes[idx].count = n; return idx; };
        if (n <= 2 || ext[axis] <= 0) return make_leaf();

        // Binned SAH along the widest centroid axis.
        constexpr int B = 12;
        AABB bbox[B];
        int bcount[B] = {};
        auto bin = [&](const Prim& p) { return std::min(B - 1, int(B * (p.c[axis] - cbox.lo[axis]) / ext[axis])); };
        for (int i = start; i < end; ++i) { int b = bin(prims[i]); bcount[b]++; bbox[b].grow(prims[i].box); }
        float best = 1e30f;
        int split = -1;
        for (int s = 1; s < B; ++s) {
            AABB l, r;
            int nl = 0, nr = 0;
            for (int b = 0; b < s; ++b) { l.grow(bbox[b]); nl += bcount[b]; }
            for (int b = s; b < B; ++b) { r.grow(bbox[b]); nr += bcount[b]; }
            float cost = (nl ? l.area() * nl : 0) + (nr ? r.area() * nr : 0);
            if (cost < best) { best = cost; split = s; }
        }
        if (n <= 4 && best >= box.area() * n) return make_leaf();  // splitting doesn't pay
        int mid = int(std::partition(prims.begin() + start, prims.begin() + end,
                                     [&](const Prim& p) { return bin(p) < split; }) - prims.begin());
        if (mid == start || mid == end) {  // all in one bin: split by count instead
            mid = (start + end) / 2;
            std::nth_element(prims.begin() + start, prims.begin() + mid, prims.begin() + end,
                             [&](const Prim& a, const Prim& b) { return a.c[axis] < b.c[axis]; });
        }
        nodes[idx].count = 0;
        nodes[idx].axis = axis;
        build_node(start, mid);
        int right = build_node(mid, end);
        nodes[idx].right = right;
        return idx;
    }
};
