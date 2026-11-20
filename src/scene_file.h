#pragma once
// Line-based scene files. One statement per line, '#' starts a comment. Units are free.
//
//   image W H                    spp N                    exposure E
//   camera fx fy fz  ax ay az  vfov_deg
//   background <spectrum>        renderer pt | hybrid LIGHT_PATHS_PER_PIXEL | bdpt
//   clamp Y                      (max luminance per sample, removes fireflies; biased; 0 = off)
//   material NAME diffuse <spectrum>
//   material NAME mirror <spectrum>
//   material NAME conductor gold|silver|copper
//   material NAME dielectric bk7 | sf11 | ior N | cauchy A B
//   material NAME light <spectrum> SCALE [two_sided]
//   sphere cx cy cz r MAT
//   quad qx qy qz ux uy uz vx vy vz MAT        (emits along u x v if MAT is a light)
//   tri ax ay az bx by bz cx cy cz MAT
//   box px py pz sx sy sz rot_y_deg MAT
//   obj PATH height x z base MAT                (path relative to the working directory)
//
// <spectrum>: rgb R G B | const V | blackbody K | d65 | gaussian MU SIGMA
#include "bdpt.h"
#include "camera.h"
#include "geometry.h"
#include "material.h"
#include "metals.h"
#include "obj.h"
#include "spectrum.h"
#include <fstream>
#include <map>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

struct SceneDesc {
    Scene scene;
    std::vector<Material> mats;
    Camera cam{{0, 0, 5}, {0, 0, 0}, {0, 1, 0}, 40, 1};
    int W = 640, H = 360, spp = 64;
    float exposure = 1;
    Spectrum background = constant(0.0f);
    bool hybrid = false, bdpt = false;
    int light_paths = 256;
};

// Returns false and sets err ("file:line: message") on any malformed line.
inline bool load_scene(const std::string& path, SceneDesc& d, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "cannot open " + path; return false; }
    std::map<std::string, int> names;
    bool have_camera = false;
    Vec3 cam_from, cam_at;
    float vfov = 40;
    std::string line;
    for (int lineno = 1; std::getline(f, line); ++lineno) {
        if (auto c = line.find('#'); c != std::string::npos) line.resize(c);
        std::istringstream ss(line);
        std::string kw;
        if (!(ss >> kw)) continue;
        auto fail = [&](const std::string& msg) { err = path + ":" + std::to_string(lineno) + ": " + msg; return false; };
        auto num = [&](float& v) { return bool(ss >> v); };
        auto vec = [&](Vec3& v) { return bool(ss >> v.x >> v.y >> v.z); };
        auto mat = [&](int& m) {
            std::string n;
            if (!(ss >> n)) return false;
            auto it = names.find(n);
            if (it == names.end()) return false;
            m = it->second;
            return true;
        };
        auto spectrum = [&](Spectrum& s) {
            std::string t;
            if (!(ss >> t)) return false;
            float a, b, c;
            if (t == "rgb" && ss >> a >> b >> c) s = rgb_spectrum(a, b, c);
            else if (t == "const" && ss >> a) s = constant(a);
            else if (t == "blackbody" && ss >> a && a > 0) s = blackbody(a);
            else if (t == "d65") s = d65();
            else if (t == "gaussian" && ss >> a >> b && b > 0) s = gaussian(a, b);
            else return false;
            return true;
        };

        if (kw == "image") {
            if (!(ss >> d.W >> d.H) || d.W <= 0 || d.H <= 0 || d.W > 16384 || d.H > 16384) return fail("image W H");
        } else if (kw == "spp") {
            if (!(ss >> d.spp) || d.spp <= 0) return fail("spp N (N > 0)");
        } else if (kw == "exposure") {
            if (!num(d.exposure)) return fail("exposure E");
        } else if (kw == "clamp") {
            if (!num(d.scene.clamp) || d.scene.clamp < 0) return fail("clamp Y (Y >= 0)");
        } else if (kw == "camera") {
            if (!vec(cam_from) || !vec(cam_at) || !num(vfov) || vfov <= 0 || vfov >= 180) return fail("camera from(3) at(3) vfov");
            have_camera = true;
        } else if (kw == "background") {
            if (!spectrum(d.background)) return fail("background <spectrum>");
        } else if (kw == "renderer") {
            std::string r;
            ss >> r;
            d.hybrid = d.bdpt = false;
            if (r == "pt") {
            } else if (r == "bdpt") d.bdpt = true;
            else if (r == "hybrid" && ss >> d.light_paths && d.light_paths > 0) d.hybrid = true;
            else return fail("renderer pt | hybrid LIGHT_PATHS_PER_PIXEL | bdpt");
        } else if (kw == "material") {
            std::string name, kind;
            if (!(ss >> name >> kind)) return fail("material NAME KIND ...");
            if (names.count(name)) return fail("material '" + name + "' already defined");
            Material m;
            if (kind == "diffuse") {
                if (!spectrum(m.albedo)) return fail("material NAME diffuse <spectrum>");
            } else if (kind == "mirror") {
                if (!spectrum(m.albedo)) return fail("material NAME mirror <spectrum>");
                m.kind = Kind::Mirror;
            } else if (kind == "conductor") {
                std::string metal;
                ss >> metal;
                if (metal == "gold") m = conductor(au_eta(), au_k());
                else if (metal == "silver") m = conductor(ag_eta(), ag_k());
                else if (metal == "copper") m = conductor(cu_eta(), cu_k());
                else return fail("conductor gold|silver|copper");
            } else if (kind == "dielectric") {
                std::string g;
                float a, b;
                ss >> g;
                if (g == "bk7") m = dielectric(glass_bk7());
                else if (g == "sf11") m = dielectric(glass_sf11());
                else if (g == "ior" && ss >> a && a > 0) m = dielectric(constant(a));
                else if (g == "cauchy" && ss >> a >> b && a > 0) m = dielectric(cauchy(a, b));
                else return fail("dielectric bk7 | sf11 | ior N | cauchy A B");
            } else if (kind == "light") {
                if (!spectrum(m.emission) || !num(m.emission_scale) || m.emission_scale <= 0)
                    return fail("material NAME light <spectrum> SCALE [two_sided]");
                std::string opt;
                if (ss >> opt) {
                    if (opt != "two_sided") return fail("unknown light option '" + opt + "'");
                    m.two_sided = true;
                }
            } else {
                return fail("unknown material kind '" + kind + "'");
            }
            names[name] = int(d.mats.size());
            d.mats.push_back(m);
        } else if (kw == "sphere") {
            Sphere s;
            if (!vec(s.c) || !num(s.r) || s.r <= 0 || !mat(s.mat)) return fail("sphere cx cy cz r MATERIAL");
            d.scene.spheres.push_back(s);
        } else if (kw == "quad") {
            Quad q;
            if (!vec(q.q) || !vec(q.u) || !vec(q.v) || !mat(q.mat)) return fail("quad q(3) u(3) v(3) MATERIAL");
            d.scene.quads.push_back(q);
        } else if (kw == "tri") {
            Triangle t;
            if (!vec(t.a) || !vec(t.b) || !vec(t.c) || !mat(t.mat)) return fail("tri a(3) b(3) c(3) MATERIAL");
            d.scene.tris.push_back(t);
        } else if (kw == "box") {
            Vec3 p, s;
            float deg;
            int m;
            if (!vec(p) || !vec(s) || !num(deg) || !mat(m)) return fail("box p(3) size(3) rot_y_deg MATERIAL");
            float r = deg * std::numbers::pi_v<float> / 180, c = std::cos(r), sn = std::sin(r);
            add_box(d.scene.quads, p, Vec3{c, 0, -sn} * s.x, {0, s.y, 0}, Vec3{sn, 0, c} * s.z, m);
        } else if (kw == "obj") {
            std::string file;
            float h, x, z, base;
            int m;
            if (!(ss >> file) || !num(h) || h <= 0 || !num(x) || !num(z) || !num(base) || !mat(m))
                return fail("obj PATH height x z base MATERIAL");
            std::vector<Triangle> tris;
            std::string oerr;
            if (!load_obj(file, m, tris, oerr)) return fail(oerr);
            fit_mesh(tris, h, x, z, base);
            d.scene.tris.insert(d.scene.tris.end(), tris.begin(), tris.end());
        } else {
            return fail("unknown statement '" + kw + "'");
        }
        std::string extra;
        if (ss >> extra) return fail("unexpected '" + extra + "'");
    }
    if (have_camera) d.cam = Camera(cam_from, cam_at, {0, 1, 0}, vfov, float(d.W) / d.H);
    return true;
}

// Render with whichever renderer the description asks for. spp_override (if > 0) replaces
// spp for pt/bdpt and light paths per pixel for hybrid.
inline Image render_desc(const SceneDesc& d, int spp_override = 0, unsigned seed = 1) {
    if (d.bdpt) return render_bdpt(d.scene, d.mats, d.background, d.cam, d.W, d.H, spp_override ? spp_override : d.spp, d.exposure, seed);
    if (d.hybrid)
        return render_hybrid(d.scene, d.mats, d.cam, d.W, d.H, d.spp,
                             long(spp_override ? spp_override : d.light_paths) * d.W * d.H, d.exposure, seed);
    return render(d.scene, d.mats, d.background, d.cam, d.W, d.H, spp_override ? spp_override : d.spp, d.exposure, seed);
}
