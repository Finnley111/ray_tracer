#include "color.h"
#include "image.h"
#include "spectrum.h"
#include "geometry.h"
#include "camera.h"
#include "integrator.h"
#include "metals.h"
#include "obj.h"
#include "scene_file.h"
#include "bdpt.h"
#include <random>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>

static bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

int main() {
    // RT-002: y-bar peaks ~1 at 555 nm
    assert(near(cie_cmf(555.0f)[1], 1.0f, 0.02f));

    // RT-002: equal-energy illuminant -> chromaticity (1/3, 1/3), Y normalised to 1
    XYZ e = spectrum_to_xyz([](float) { return 1.0f; });
    float s = e[0] + e[1] + e[2];
    assert(near(e[1], 1.0f, 1e-4f));
    assert(near(e[0] / s, 1.0f / 3, 0.01f) && near(e[1] / s, 1.0f / 3, 0.01f));

    // RT-002: sRGB transfer function endpoints + clamping
    assert(srgb_encode(0) == 0 && near(srgb_encode(1), 1, 1e-6f) && srgb_encode(2) == srgb_encode(1));
    assert(near(srgb_encode(0.5f), 0.7354f, 1e-3f));

    // RT-001: PPM header + size
    Image img(3, 2);
    img.at(2, 1) = {1, 0, 0};
    const char* p = "rt_tests_tmp.ppm";
    assert(write_ppm(img, p));
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    assert(f.tellg() == std::streamoff(11 + 3 * 2 * 3));  // "P6\n3 2\n255\n" + pixels
    std::remove(p);

    // RT-003: spectrum types
    assert(near(blackbody(5000)(2.897771955e-3f / 5000 * 1e9f), 1.0f, 1e-4f));
    assert(blackbody(5000)(400) < 1 && blackbody(5000)(700) < 1);
    auto lin = sampled({{400, 0}, {500, 1}});
    assert(lin(300) == 0 && near(lin(450), 0.5f, 1e-6f) && lin(900) == 1);
    assert(near(gaussian(550, 10)(550), 1, 1e-6f));
    XYZ d = spectrum_to_xyz(d65());
    float ds = d[0] + d[1] + d[2];
    std::printf("D65 xy = (%.4f, %.4f)\n", d[0] / ds, d[1] / ds);
    assert(near(d[0] / ds, 0.3127f, 0.005f) && near(d[1] / ds, 0.3290f, 0.005f));

    // RT-004: wavelengths stay in range and are spread by range/4
    for (float u : {0.0f, 0.3f, 0.999f}) {
        auto w = SampledWavelengths::sample_uniform(u);
        for (float l : w.lambda) assert(l >= LAMBDA_MIN && l < LAMBDA_MAX);
    }
    // RT-004: MC estimate of D65 matches the deterministic integral to <1%
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> U(0, 1);
        Spectrum s = d65();
        const int N = 200000;
        double acc[3] = {0, 0, 0};
        for (int i = 0; i < N; ++i) {
            auto w = SampledWavelengths::sample_uniform(U(rng));
            XYZ c = to_xyz(w.eval(s), w);
            for (int k = 0; k < 3; ++k) acc[k] += c[k] / N;
        }
        for (int k = 0; k < 3; ++k) assert(std::fabs(acc[k] - d[k]) / d[k] < 0.01);
    }

    // RT-005: sphere hit from outside, miss, and from inside
    {
        Sphere sp{{0, 0, -5}, 1, 3};
        Hit h;
        assert(sp.intersect({{0, 0, 0}, {0, 0, -1}}, 1e-4f, 1e30f, h));
        assert(near(h.t, 4, 1e-5f) && h.front && near(h.n.z, 1, 1e-5f) && h.mat == 3);
        assert(!sp.intersect({{0, 0, 0}, {0, 1, 0}}, 1e-4f, 1e30f, h));
        assert(!sp.intersect({{0, 0, 0}, {0, 0, -1}}, 1e-4f, 3.0f, h));  // beyond tmax
        assert(sp.intersect({{0, 0, -5}, {1, 0, 0}}, 1e-4f, 1e30f, h));  // from inside
        assert(near(h.t, 1, 1e-5f) && !h.front && near(h.n.x, -1, 1e-5f));
    }
    // RT-005: quad hit inside bounds, miss outside, parallel ray misses
    {
        Quad q{{-1, -1, -2}, {2, 0, 0}, {0, 2, 0}, 0};  // u x v = +z, faces the camera
        Hit h;
        assert(q.intersect({{0.5f, 0.5f, 0}, {0, 0, -1}}, 1e-4f, 1e30f, h));
        assert(near(h.t, 2, 1e-5f) && h.front && near(h.n.z, 1, 1e-5f));
        assert(!q.intersect({{1.5f, 0, 0}, {0, 0, -1}}, 1e-4f, 1e30f, h));
        assert(!q.intersect({{0, 0, 0}, {1, 0, 0}}, 1e-4f, 1e30f, h));
    }
    // RT-005: scene returns the closest hit regardless of list order
    {
        Scene sc;
        sc.quads = {{{-1, -1, -2}, {2, 0, 0}, {0, 2, 0}, 1}};
        sc.spheres = {{{0, 0, -10}, 1, 2}};
        Hit h;
        assert(sc.intersect({{0, 0, 0}, {0, 0, -1}}, 1e-4f, 1e30f, h) && h.mat == 1);
    }
    // RT-006: centre ray points at look-at; top-left ray goes up and left
    {
        Camera cam({0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 90, 2.0f);
        Ray c = cam.ray(0.5f, 0.5f), tl = cam.ray(0, 0);
        assert(near(c.d.z, -1, 1e-5f));
        assert(tl.d.x < 0 && tl.d.y > 0 && near(tl.d.y / -tl.d.z, 1, 1e-5f));  // 90 deg vfov
    }

    // RT-007: cosine samples are unit length and in the hemisphere, mean cos = 2/3
    {
        Rng rng(3);
        Vec3 n = normalize(Vec3{0.3f, -0.8f, 0.5f});
        double mc = 0;
        for (int i = 0; i < 100000; ++i) {
            Vec3 d = sample_cosine_hemisphere(n, rng.next(), rng.next());
            assert(near(length(d), 1, 1e-4f) && dot(d, n) >= -1e-5f);
            mc += dot(d, n) / 100000;
        }
        assert(near(float(mc), 2.0f / 3, 0.01f));
    }
    // RT-007: furnace tests. Mean radiance per wavelength must match the analytic answer.
    auto furnace = [](const Scene& sc, const std::vector<Material>& mats, const Spectrum& bg, Ray r) {
        Rng rng(11);
        double sum = 0;
        const int N = 100000;
        for (int i = 0; i < N; ++i) {
            auto w = SampledWavelengths::sample_uniform(rng.next());
            SampledSpectrum L = trace(sc, mats, bg, r, w, rng);
            // pdf-weighted, so it stays right when trace() terminates secondary wavelengths
            for (int j = 0; j < N_LAMBDA; ++j)
                if (w.pdf[j] > 0) sum += L[j] / (w.pdf[j] * (LAMBDA_MAX - LAMBDA_MIN)) / (N_LAMBDA * double(N));
        }
        return float(sum);
    };
    {
        // White sphere (albedo 1) under uniform sky of 1 -> 1. Albedo 0.5 -> 0.5.
        Scene sc;
        sc.spheres = {{{0, 0, -3}, 1, 0}};
        Ray r{{0, 0, 0}, {0, 0, -1}};
        float a = furnace(sc, {{constant(1.0f)}}, constant(1.0f), r);
        float b = furnace(sc, {{constant(0.5f)}}, constant(1.0f), r);
        std::printf("furnace outside: %.4f (1), %.4f (0.5)\n", a, b);
        assert(near(a, 1, 0.01f) && near(b, 0.5f, 0.01f));
    }
    {
        // Inside a closed sphere: albedo 0.8, emission 0.2 -> L = e / (1 - a) = 1.
        // Exercises many bounces and Russian roulette.
        Scene sc;
        sc.spheres = {{{0, 0, 0}, 1, 0}};
        Material glow{constant(0.8f), constant(1.0f), 0.2f};
        glow.two_sided = true;  // seen from inside
        float c = furnace(sc, {glow}, constant(0.0f), {{0, 0, 0}, {1, 0, 0}});
        std::printf("furnace inside: %.4f (1)\n", c);
        assert(near(c, 1, 0.02f));
    }

    // RT-014: image is bit-identical regardless of thread count
    {
        Scene sc;
        sc.spheres = {{{0, 0, -3}, 1, 0}};
        std::vector<Material> mats = {{constant(0.5f)}};
        Camera cam({0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 60, 1);
        Image a = render(sc, mats, d65(), cam, 16, 16, 8, 1, 5, 1);
        Image b = render(sc, mats, d65(), cam, 16, 16, 8, 1, 5, 4);
        assert(a.px == b.px);
    }

    // RT-010: Fresnel. Glass at normal incidence 4%; conductor matches the closed form; grazing -> 1.
    {
        assert(near(fresnel_conductor(1, 1.5f, 0), 0.04f, 1e-5f));
        float n = 0.43f, k = 2.455f;  // gold @ 549 nm
        assert(near(fresnel_conductor(1, n, k), ((n - 1) * (n - 1) + k * k) / ((n + 1) * (n + 1) + k * k), 1e-5f));
        assert(near(fresnel_conductor(0, n, k), 1, 1e-4f));
    }
    // RT-010: normal-incidence reflectance under D65 gives the right metal colours.
    {
        auto colour = [](Spectrum eta, Spectrum k) {
            Spectrum d = d65();
            return xyz_to_linear_srgb(spectrum_to_xyz([&](float l) { return d(l) * fresnel_conductor(1, eta(l), k(l)); }));
        };
        RGB au = colour(au_eta(), au_k()), cu = colour(cu_eta(), cu_k()), ag = colour(ag_eta(), ag_k());
        std::printf("Au %.3f %.3f %.3f | Cu %.3f %.3f %.3f | Ag %.3f %.3f %.3f\n",
                    au[0], au[1], au[2], cu[0], cu[1], cu[2], ag[0], ag[1], ag[2]);
        assert(au[0] > au[1] && au[1] > au[2] && au[0] > 1.5f * au[2]);       // gold: yellow
        assert(cu[0] > 1.3f * cu[1] && cu[1] > cu[2]);                          // copper: orange-red
        assert(near(ag[0] / ag[2], 1, 0.15f) && ag[1] > 0.85f);                // silver: bright, neutral
    }
    {
        // Mirror furnace: a 100% mirror sphere under a sky of 1 -> 1.
        Scene sc;
        sc.spheres = {{{0, 0, -3}, 1, 0}};
        float m = furnace(sc, {mirror(constant(1.0f))}, constant(1.0f), {{0, 0, 0}, normalize(Vec3{0.3f, 0.2f, -1})});
        assert(near(m, 1, 1e-4f));
    }

    // RT-011: Schott catalogue n_d (587.6 nm) and a dispersion sanity check
    assert(near(glass_bk7()(587.56f), 1.5168f, 2e-4f));
    assert(near(glass_sf11()(587.56f), 1.78472f, 2e-4f));
    assert(glass_sf11()(450) > glass_sf11()(650) && near(cauchy(1.5f, 0.01f)(1000), 1.51f, 1e-6f));
    // RT-011: refraction obeys Snell's law
    {
        Vec3 n{0, 1, 0}, d = normalize(Vec3{1, -1, 0});  // 45 degrees in
        Vec3 t = refract(d, n, 1.5f);
        assert(near(length(t), 1, 1e-5f) && near(t.x, std::sin(0.785398f) / 1.5f, 1e-5f) && t.y < 0);
    }
    // RT-011: terminate_secondary keeps the estimator unbiased and is idempotent
    {
        auto w = SampledWavelengths::sample_uniform(0.25f);
        float p0 = w.pdf[0];
        w.terminate_secondary();
        w.terminate_secondary();
        assert(w.pdf[1] == 0 && w.pdf[3] == 0 && near(w.pdf[0], p0 / N_LAMBDA, 1e-9f));
    }
    // RT-011: glass furnace. Lossless glass sphere under a sky of 1 -> 1, dispersive or not.
    {
        Scene sc;
        sc.spheres = {{{0, 0, -3}, 1, 0}};
        Ray r{{0, 0, 0}, normalize(Vec3{0.2f, 0.1f, -1})};
        float g = furnace(sc, {dielectric(constant(1.5f))}, constant(1.0f), r);
        float f = furnace(sc, {dielectric(glass_sf11())}, constant(1.0f), r);
        std::printf("glass furnace: %.4f (1), dispersive %.4f (1)\n", g, f);
        assert(near(g, 1, 0.01f) && near(f, 1, 0.02f));
    }

    // RT-012: triangle hit/miss, normal from winding, orient_convex
    {
        Triangle t{{0, 0, -2}, {1, 0, -2}, {0, 1, -2}, 0};  // (b-a)x(c-a) = +z
        Hit h;
        assert(t.intersect({{0.2f, 0.2f, 0}, {0, 0, -1}}, 1e-4f, 1e30f, h));
        assert(near(h.t, 2, 1e-5f) && h.front && near(h.n.z, 1, 1e-5f));
        assert(!t.intersect({{0.8f, 0.8f, 0}, {0, 0, -1}}, 1e-4f, 1e30f, h));  // outside u+v<=1
        assert(!t.intersect({{0.2f, 0.2f, 0}, {1, 0, 0}}, 1e-4f, 1e30f, h));   // parallel
        // Tetrahedron with one face wound inward: orient_convex must fix it.
        Vec3 p0{0, 0, 0}, p1{1, 0, 0}, p2{0, 1, 0}, p3{0, 0, 1};
        std::vector<Triangle> tet = {{p0, p1, p2, 0}, {p0, p1, p3, 0}, {p0, p2, p3, 0}, {p1, p2, p3, 0}};
        orient_convex(tet);
        Vec3 c{0.25f, 0.25f, 0.25f};
        for (auto& f : tet) assert(dot(cross(f.b - f.a, f.c - f.a), f.a - c) > 0);
    }

    // RT-012: project() inverts ray()
    {
        Camera cam({1, 2, 3}, {0, 0, 0}, {0, 1, 0}, 50, 1.5f);
        float u, v, c;
        Ray r = cam.ray(0.3f, 0.8f);
        assert(cam.project(r.at(5), u, v, c) && near(u, 0.3f, 1e-4f) && near(v, 0.8f, 1e-4f));
        assert(near(c, dot(r.d, cam.fwd), 1e-5f));
        assert(!cam.project(cam.origin - cam.fwd, u, v, c));
    }
    // RT-012: hybrid (camera paths + light paths) agrees with the pure path tracer.
    {
        std::vector<Material> mats = {{constant(0.5f)}, {[](float l) { return 0.1f + 0.7f * gaussian(600, 40)(l); }},
                                      {constant(0.0f), d65(), 5.0f}};
        Scene sc;
        sc.quads = {{{-3, 0, -3}, {0, 0, 6}, {6, 0, 0}, 0}, {{-0.5f, 2, -0.5f}, {1, 0, 0}, {0, 0, 1}, 2}};
        sc.spheres = {{{0.6f, 0.5f, 0.3f}, 0.5f, 1}};
        Camera cam({0, 1.5f, 4}, {0, 0.3f, 0}, {0, 1, 0}, 45, 1);
        Image pt = render(sc, mats, constant(0.0f), cam, 24, 24, 512, 1);
        Image hy = render_hybrid(sc, mats, cam, 24, 24, 4, 2000000, 1);
        double a[3] = {0, 0, 0}, b[3] = {0, 0, 0};
        for (size_t i = 0; i < pt.px.size(); ++i)
            for (int k = 0; k < 3; ++k) a[k] += pt.px[i][k], b[k] += hy.px[i][k];
        std::printf("PT vs hybrid mean RGB: %.4f %.4f %.4f | %.4f %.4f %.4f\n", a[0] / pt.px.size(), a[1] / pt.px.size(),
                    a[2] / pt.px.size(), b[0] / pt.px.size(), b[1] / pt.px.size(), b[2] / pt.px.size());
        for (int k = 0; k < 3; ++k) assert(std::fabs(a[k] - b[k]) / a[k] < 0.03);
    }

    // RT-013: BVH returns exactly what the linear scan returns, for random rays in a random scene.
    {
        Rng rng(42);
        auto rv = [&](float s) { return Vec3{(rng.next() - 0.5f) * s, (rng.next() - 0.5f) * s, (rng.next() - 0.5f) * s}; };
        Scene sc;
        for (int i = 0; i < 100; ++i) sc.spheres.push_back({rv(20), 0.1f + rng.next(), i});
        for (int i = 0; i < 100; ++i) sc.quads.push_back({rv(20), rv(3), rv(3), 100 + i});
        for (int i = 0; i < 100; ++i) { Vec3 a = rv(20); sc.tris.push_back({a, a + rv(3), a + rv(3), 200 + i}); }
        sc.quads.push_back({{-10, -10, 0}, {20, 0, 0}, {0, 20, 0}, 999});  // axis-aligned, zero-thickness
        Scene bvh = sc;
        bvh.build();
        assert(bvh.built() && !sc.built());
        int hits = 0;
        for (int i = 0; i < 20000; ++i) {
            Ray r{rv(30), normalize(rv(2))};
            if (i % 10 == 0) r.d = {0, 0, r.d.z < 0 ? -1.0f : 1.0f};  // axis-parallel rays: inv = inf
            Hit a, b;
            bool ha = sc.intersect(r, 1e-4f, 1e30f, a), hb = bvh.intersect(r, 1e-4f, 1e30f, b);
            assert(ha == hb);
            if (ha) { ++hits; assert(a.t == b.t && a.mat == b.mat); }
        }
        std::printf("BVH vs linear: 20000 rays, %d hits, all identical\n", hits);
    }

    // RT-015: OBJ loader. Slash forms, negative indices, fan triangulation, comments; errors.
    {
        const char* path = "rt_tests_tmp.obj";
        std::ofstream(path) << "# square + triangle\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvn 0 0 1\n"
                               "f 1/1/1 2/2/1 3//1 4\nf -4 -3 -1\no ignored\n";
        std::vector<Triangle> t;
        std::string err;
        assert(load_obj(path, 7, t, err) && t.size() == 3);
        assert(t[0].c.x == 1 && t[0].c.y == 1 && t[1].c.x == 0 && t[1].c.y == 1);  // fan: (1,2,3), (1,3,4)
        assert(t[2].a.x == 0 && t[2].b.x == 1 && t[2].c.y == 1 && t[2].mat == 7);   // -4 -3 -1 = 1 2 4
        std::ofstream(path) << "v 0 0 0\nv 1 0 0\nf 1 2 3\n";
        assert(!load_obj(path, 0, t, err) && t.size() == 3 && err.find(":3:") != std::string::npos);
        std::ofstream(path) << "v 0 0 0\nv 1 0 0\nv 1 1 0\nf 1 2\n";
        assert(!load_obj(path, 0, t, err));
        assert(!load_obj("does_not_exist.obj", 0, t, err));
        std::remove(path);
        fit_mesh(t, 2, 5, 0, 1);  // 1x1 square -> 2 tall, centred on x = 5, resting on y = 1
        AABB b;
        for (auto& tr : t) { b.grow(tr.a); b.grow(tr.b); b.grow(tr.c); }
        assert(near(b.lo.y, 1, 1e-5f) && near(b.hi.y, 3, 1e-5f) && near((b.lo.x + b.hi.x) / 2, 5, 1e-5f));
    }

    // RT-016: NEE + MIS. Inside an emitting, reflecting sphere (sphere-light sampling from
    // inside, shadow rays, MIS on both strategies) the answer is still e / (1 - a) = 1.
    {
        Scene sc;
        sc.spheres = {{{0, 0, 0}, 1, 0}};
        std::vector<Material> mats = {{constant(0.8f), constant(1.0f), 0.2f}};
        mats[0].two_sided = true;
        sc.collect_lights(mats);
        assert(sc.lights.size() == 1);
        float c = furnace(sc, mats, constant(0.0f), {{0, 0, 0}, {1, 0, 0}});
        std::printf("furnace inside, NEE: %.4f (1)\n", c);
        assert(near(c, 1, 0.02f));
    }
    // RT-016: one-sided light: a downward panel lights the floor below, not a camera above it.
    {
        std::vector<Material> mats = {{constant(0.5f)}, {constant(0.0f), d65(), 1.0f}};
        Scene sc;
        sc.quads = {{{-1, 1, -1}, {2, 0, 0}, {0, 0, 2}, 1}};  // u x v = -y: emits downward
        sc.collect_lights(mats);
        Rng rng(1);
        auto w = SampledWavelengths::sample_uniform(0.5f);
        assert(trace(sc, mats, constant(0.0f), {{0, 0, 0}, {0, 1, 0}}, w, rng)[0] > 0);   // from below
        assert(trace(sc, mats, constant(0.0f), {{0, 2, 0}, {0, -1, 0}}, w, rng)[0] == 0);  // from above
    }
    // RT-016: NEE on and off agree in the mean (quad + sphere light, occluder).
    {
        std::vector<Material> mats = {{constant(0.6f)}, {[](float l) { return 0.1f + 0.7f * gaussian(500, 40)(l); }},
                                      {constant(0.0f), blackbody(4000), 8.0f}, {constant(0.0f), d65(), 30.0f}};
        Scene sc;
        sc.quads = {{{-3, 0, -3}, {0, 0, 6}, {6, 0, 0}, 0}, {{-0.5f, 2, -0.5f}, {1, 0, 0}, {0, 0, 1}, 2}};
        sc.spheres = {{{0.5f, 0.5f, 0.2f}, 0.5f, 1}, {{-1.2f, 1.2f, 0}, 0.15f, 3}};
        Camera cam({0, 1.5f, 4}, {0, 0.3f, 0}, {0, 1, 0}, 45, 1);
        Image on = render(sc, mats, constant(0.0f), cam, 24, 24, 256, 1);
        sc.use_nee = false;
        Image off = render(sc, mats, constant(0.0f), cam, 24, 24, 4096, 1);
        double a[3] = {0, 0, 0}, b[3] = {0, 0, 0};
        for (size_t i = 0; i < on.px.size(); ++i)
            for (int k = 0; k < 3; ++k) a[k] += on.px[i][k], b[k] += off.px[i][k];
        std::printf("NEE on vs off mean RGB: %.4f %.4f %.4f | %.4f %.4f %.4f\n", a[0] / on.px.size(),
                    a[1] / on.px.size(), a[2] / on.px.size(), b[0] / on.px.size(), b[1] / on.px.size(),
                    b[2] / on.px.size());
        for (int k = 0; k < 3; ++k) assert(std::fabs(a[k] - b[k]) / b[k] < 0.02);
    }

    // RT-017: Smits RGB -> spectrum round trip. Reflectance under D65 (normalised so white
    // -> Y = 1) back to linear sRGB should land near the input colour.
    {
        Spectrum illum = d65();
        float yn = spectrum_to_xyz(illum)[1];
        auto round_trip = [&](float r, float g, float b) {
            Spectrum refl = rgb_spectrum(r, g, b);
            XYZ c = spectrum_to_xyz([&](float l) { return illum(l) * refl(l); });
            return xyz_to_linear_srgb({c[0] / yn, c[1] / yn, c[2] / yn});
        };
        float worst = 0;
        for (RGB in : {RGB{1, 1, 1}, RGB{0.73f, 0.73f, 0.73f}, RGB{0.65f, 0.05f, 0.05f}, RGB{0.12f, 0.45f, 0.15f},
                       RGB{0.2f, 0.3f, 0.8f}, RGB{0.9f, 0.8f, 0.1f}, RGB{0.5f, 0.2f, 0.6f}}) {
            RGB out = round_trip(in[0], in[1], in[2]);
            for (int k = 0; k < 3; ++k) worst = std::max(worst, std::fabs(out[k] - in[k]));
            std::printf("rgb (%.2f %.2f %.2f) -> (%.3f %.3f %.3f)\n", in[0], in[1], in[2], out[0], out[1], out[2]);
        }
        std::printf("Smits round trip worst channel error %.3f\n", worst);
        assert(worst < 0.08f);
        Spectrum white = rgb_spectrum(1, 1, 1);
        assert(white(500) > 0.99f && white(380) > 0.99f);
    }

    // RT-018: PNG writer. Signature, IHDR size, stored-deflate length, IEND CRC.
    {
        Image img(70, 1000);  // raw = 1000 * 211 bytes -> 4 stored blocks
        const char* p = "rt_tests_tmp.png";
        assert(write_image(img, p));
        std::ifstream f(p, std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(f)), {});
        std::remove(p);
        size_t raw = 1000 * (1 + 3 * 70), blocks = (raw + 65534) / 65535;
        assert(data.size() == 8 + 25 + 12 + (2 + 5 * blocks + raw + 4) + 12);
        assert(data.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0 && data.compare(12, 4, "IHDR") == 0);
        assert(data.compare(data.size() - 4, 4, "\xae\x42\x60\x82") == 0);  // CRC of "IEND"
    }
    // RT-018: scene file parsing, plus errors with line numbers.
    {
        const char* p = "rt_tests_tmp_scene.txt";
        std::ofstream(p) << "# test\nimage 32 16\nspp 8\nclamp 20\ncamera 0 1 5  0 0 0  45\nbackground const 0.5\n"
                            "material w diffuse rgb 0.5 0.5 0.5\nmaterial l light blackbody 3000 4 two_sided\n"
                            "material g dielectric cauchy 1.5 0.01\nsphere 0 0 0 1 g  # inline comment\n"
                            "quad -1 2 -1 2 0 0 0 0 2 l\nbox 0 0 0 1 1 1 30 w\ntri 0 0 0 1 0 0 0 1 0 w\n";
        SceneDesc d;
        std::string err;
        assert(load_scene(p, d, err));
        assert(d.W == 32 && d.H == 16 && d.spp == 8 && d.scene.clamp == 20 && d.mats.size() == 3 && d.mats[1].two_sided);
        assert(d.scene.spheres.size() == 1 && d.scene.quads.size() == 7 && d.scene.tris.size() == 1);
        assert(near(d.background(500), 0.5f, 1e-6f) && near(d.cam.ray(0.5f, 0.5f).d.z, -0.98058f, 1e-4f));
        auto bad = [&](const char* text, const char* expect) {
            std::ofstream(p) << text;
            SceneDesc e;
            bool ok = load_scene(p, e, err);
            if (ok || err.find(expect) == std::string::npos) std::printf("unexpected: %s\n", err.c_str());
            assert(!ok && err.find(expect) != std::string::npos);
        };
        bad("material w diffuse const 1\nsphere 0 0 0 1 nope\n", ":2:");          // unknown material
        bad("spp -3\n", ":1:");                                                   // bad value
        bad("clamp -1\n", ":1:");
        bad("material w diffuse const 1\nsphere 0 0 0 1 w extra\n", "unexpected"); // trailing junk
        bad("teapot 1 2 3\n", "unknown statement");
        bad("material a diffuse const 1\nmaterial a mirror const 1\n", "already defined");
        bad("material m dielectric diamond\n", ":1:");
        bad("obj missing.obj 1 0 0 0 w\n", ":1:");                                // material checked after path
        std::remove(p);
        assert(!load_scene("no_such_scene.txt", d, err));
    }

    // Firefly clamp: luminance capped, chromaticity kept, below-cap untouched.
    {
        XYZ c{4, 8, 2}, d{1, 2, 3};
        clamp_luminance(c, 2);
        clamp_luminance(d, 2);
        clamp_luminance(d, 0);
        assert(near(c[0], 1, 1e-6f) && near(c[1], 2, 1e-6f) && near(c[2], 0.5f, 1e-6f));
        assert(d[0] == 1 && d[1] == 2 && d[2] == 3);
    }

    // RT-019: MIS weights over all (s, t) splits of one fixed path sum to 1.
    {
        std::vector<Material> mats = {{constant(0.5f)}, {constant(0.0f), d65(), 3.0f}};
        Scene sc;
        sc.quads = {{{-2, 0, -2}, {0, 0, 4}, {4, 0, 0}, 0},     // floor, +y
                    {{-2, 0, -2}, {4, 0, 0}, {0, 3, 0}, 0},     // back wall, +z
                    {{-0.5f, 2, -0.5f}, {1, 0, 0}, {0, 0, 1}, 1}};  // light, emits -y
        sc.collect_lights(mats);
        Camera cam({0, 1, 4}, {0, 0.5f, 0}, {0, 1, 0}, 50, 1);
        Bdpt b{sc, mats, cam, 1};
        // Path: camera -> floor -> back wall -> light.
        Vec3 P[4] = {cam.origin, {0.3f, 0, 0.5f}, {-0.4f, 1.2f, -2}, {0.1f, 2, -0.2f}};
        Vec3 N[4] = {cam.fwd, {0, 1, 0}, {0, 0, 1}, {0, -1, 0}};
        auto make = [&](int i, VType type) {
            Vertex v;
            v.type = type;
            v.p = P[i];
            v.n = v.n_out = N[i];
            v.mat = i == 3 ? 1 : 0;
            v.shape = 1;
            v.area = i == 3 ? 1.0f : 16.0f;
            return v;
        };
        float total = 0;
        for (int s = 0; s <= 3; ++s) {
            int t = 4 - s;
            std::vector<Vertex> C, L;
            for (int i = 0; i < t; ++i) C.push_back(make(i, i == 0 ? VType::Camera : VType::Surface));
            for (int j = 0; j < s; ++j) L.push_back(make(3 - j, j == 0 ? VType::Light : VType::Surface));
            for (int i = 1; i < t; ++i) C[i].pdf_fwd = b.pdf(C[i - 1], i >= 2 ? &C[i - 2] : nullptr, C[i]);
            for (int i = 0; i + 2 < t; ++i) C[i].pdf_rev = b.pdf(C[i + 1], &C[i + 2], C[i]);
            if (s > 0) L[0].pdf_fwd = b.pdf_light_origin(L[0]);
            for (int j = 1; j < s; ++j) L[j].pdf_fwd = b.pdf(L[j - 1], j >= 2 ? &L[j - 2] : nullptr, L[j]);
            for (int j = 0; j + 2 < s; ++j) L[j].pdf_rev = b.pdf(L[j + 1], &L[j + 2], L[j]);
            float wgt = b.mis_weight(s, t, L, C);
            std::printf("MIS weight (s=%d, t=%d) = %.4f\n", s, t, wgt);
            assert(wgt > 0 && wgt <= 1);
            total += wgt;
        }
        std::printf("MIS weights sum: %.6f\n", total);
        assert(near(total, 1, 1e-4f));
    }
    // RT-019: BDPT agrees with the path tracer in the mean.
    {
        auto mean = [](const Image& im) {
            double m[3] = {0, 0, 0};
            for (auto& p : im.px) for (int k = 0; k < 3; ++k) m[k] += p[k] / im.px.size();
            return std::array<double, 3>{m[0], m[1], m[2]};
        };
        auto compare = [&](const char* name, const Scene& sc, const std::vector<Material>& mats, const Spectrum& bg,
                           const Camera& cam, int spp_pt, int spp_bd, double tol) {
            auto a = mean(render(sc, mats, bg, cam, 24, 24, spp_pt, 1));
            auto b = mean(render_bdpt(sc, mats, bg, cam, 24, 24, spp_bd, 1));
            std::printf("%s PT vs BDPT mean RGB: %.4f %.4f %.4f | %.4f %.4f %.4f\n", name, a[0], a[1], a[2], b[0], b[1], b[2]);
            for (int k = 0; k < 3; ++k) assert(std::fabs(a[k] - b[k]) / a[k] < tol);
        };
        // RT-016 scene: quad + sphere light, coloured sphere.
        std::vector<Material> mats = {{constant(0.6f)}, {[](float l) { return 0.1f + 0.7f * gaussian(500, 40)(l); }},
                                      {constant(0.0f), blackbody(4000), 8.0f}, {constant(0.0f), d65(), 30.0f}};
        Scene sc;
        sc.quads = {{{-3, 0, -3}, {0, 0, 6}, {6, 0, 0}, 0}, {{-0.5f, 2, -0.5f}, {1, 0, 0}, {0, 0, 1}, 2}};
        sc.spheres = {{{0.5f, 0.5f, 0.2f}, 0.5f, 1}, {{-1.2f, 1.2f, 0}, 0.15f, 3}};
        Camera cam({0, 1.5f, 4}, {0, 0.3f, 0}, {0, 1, 0}, 45, 1);
        compare("lights", sc, mats, constant(0.0f), cam, 4096, 2048, 0.02);
        // Same scene with a dispersive glass sphere and a mirror: delta vertices + wavelength termination.
        mats.push_back(dielectric(cauchy(1.5f, 0.02f)));
        mats.push_back(mirror(constant(0.9f)));
        sc.spheres.push_back({{-0.6f, 0.4f, 0.8f}, 0.4f, 4});
        sc.spheres.push_back({{1.4f, 0.6f, -0.6f}, 0.6f, 5});
        compare("glass+mirror", sc, mats, constant(0.0f), cam, 8192, 2048, 0.03);
        // Inside an emitting, reflecting sphere (two-sided), with a sky that is never seen.
        std::vector<Material> m2 = {{constant(0.8f), constant(1.0f), 0.2f}};
        m2[0].two_sided = true;
        Scene s2;
        s2.spheres = {{{0, 0, 0}, 1, 0}};
        compare("inside", s2, m2, constant(0.0f), Camera({0, 0, 0.3f}, {0, 0, -1}, {0, 1, 0}, 60, 1), 256, 128, 0.02);
    }

    std::printf("OK\n");
}
