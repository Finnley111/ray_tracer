#pragma once
// Minimal Wavefront OBJ loader: `v` and `f` only. Faces may use v, v/vt, v//vn or v/vt/vn
// and negative (relative) indices; polygons are fan-triangulated. Everything else is ignored.
#include "geometry.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Appends triangles (material mat) to out. Returns false and sets err on a missing file,
// a malformed line or an out-of-range index; out is left unchanged on failure.
inline bool load_obj(const std::string& path, int mat, std::vector<Triangle>& out, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "cannot open " + path; return false; }
    std::vector<Vec3> verts;
    std::vector<Triangle> tris;
    std::string line;
    for (int lineno = 1; std::getline(f, line); ++lineno) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v") {
            Vec3 p;
            if (!(ss >> p.x >> p.y >> p.z)) { err = path + ":" + std::to_string(lineno) + ": bad vertex"; return false; }
            verts.push_back(p);
        } else if (tag == "f") {
            std::vector<Vec3> poly;
            for (std::string tok; ss >> tok;) {
                long i;
                try { i = std::stol(tok.substr(0, tok.find('/'))); } catch (...) { i = 0; }
                long k = i < 0 ? long(verts.size()) + i : i - 1;  // OBJ is 1-based; negative = from the end
                if (i == 0 || k < 0 || k >= long(verts.size())) {
                    err = path + ":" + std::to_string(lineno) + ": bad vertex index '" + tok + "'";
                    return false;
                }
                poly.push_back(verts[k]);
            }
            if (poly.size() < 3) { err = path + ":" + std::to_string(lineno) + ": face with < 3 vertices"; return false; }
            for (size_t j = 1; j + 1 < poly.size(); ++j) tris.push_back({poly[0], poly[j], poly[j + 1], mat});
        }
    }
    out.insert(out.end(), tris.begin(), tris.end());
    return true;
}

// Uniformly scale and move triangles so their bounding box is `height` tall, centred on
// (x, z) and resting on y = base.
inline void fit_mesh(std::vector<Triangle>& tris, float height, float x, float z, float base) {
    AABB b;
    for (auto& t : tris) { b.grow(t.a); b.grow(t.b); b.grow(t.c); }
    float s = height / (b.hi.y - b.lo.y);
    Vec3 off = Vec3{x, base, z} - Vec3{(b.lo.x + b.hi.x) / 2 * s, b.lo.y * s, (b.lo.z + b.hi.z) / 2 * s};
    for (auto& t : tris) { t.a = t.a * s + off; t.b = t.b * s + off; t.c = t.c * s + off; }
}
