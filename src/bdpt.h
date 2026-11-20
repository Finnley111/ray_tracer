#pragma once
// Bidirectional path tracing with MIS (Veach 1997 ch. 10; structure follows pbrt-v4 §13.3).
// A camera subpath and a light subpath are traced per pixel sample; every pair of their
// vertices is connected (strategy (s, t) = s light vertices + t camera vertices) and the
// strategies are combined with the balance heuristic, computed from pdf ratios.
//
// Wavelengths: one SampledWavelengths per sample, shared by both subpaths. Crossing
// dispersive glass on either side terminates the secondaries for the whole sample
// (unbiased; as pbrt-v4). Contributions are converted with to_xyz after both subpaths exist.
// Lights: emissive spheres and quads (scene.lights). Emissive triangles are only found by
// camera paths (weight 1). The background is only found by escaping camera paths (weight 1).
#include "integrator.h"

enum class VType { Camera, Light, Surface };

struct Vertex {
    VType type = VType::Surface;
    Vec3 p;
    Vec3 n;      // geometric normal; surface: facing the side its subpath arrived from
    Vec3 n_out;  // outward normal (decides which side an emitter emits from)
    int mat = -1;
    int shape = 0;    // 2 = triangle (not light-sampled)
    float area = 0;   // area of the shape the vertex lies on
    bool delta = false;
    SampledSpectrum beta;
    float pdf_fwd = 0, pdf_rev = 0;  // area densities: from its own subpath / from the other side
    bool on_surface() const { return type != VType::Camera; }
};

struct Bdpt {
    const Scene& scene;
    const std::vector<Material>& mats;
    const Camera& cam;
    float n_lights;
    static constexpr float INV_PI = std::numbers::inv_pi_v<float>;

    bool emissive(const Vertex& v) const { return v.mat >= 0 && mats[v.mat].emission_scale > 0; }

    // Radiance emitted from v toward point `to`.
    SampledSpectrum Le(const Vertex& v, Vec3 to, const SampledWavelengths& w) const {
        SampledSpectrum r;
        if (!emissive(v)) return r;
        const Material& m = mats[v.mat];
        if (!m.two_sided && dot(to - v.p, v.n_out) <= 0) return r;
        return w.eval(m.emission) * m.emission_scale;
    }

    // BSDF at surface vertex v between the directions toward points a and b (Lambertian only;
    // specular materials are delta and never connected).
    SampledSpectrum f(const Vertex& v, Vec3 a, Vec3 b, const SampledWavelengths& w) const {
        SampledSpectrum r;
        const Material& m = mats[v.mat];
        if (m.kind != Kind::Diffuse || dot(a - v.p, v.n) * dot(b - v.p, v.n) <= 0) return r;
        return w.eval(m.albedo) * INV_PI;
    }

    // Solid-angle pdf -> area pdf at `to`.
    float convert(float pdf_dir, const Vertex& from, const Vertex& to) const {
        Vec3 d = to.p - from.p;
        float d2 = dot(d, d);
        if (d2 == 0) return 0;
        float c = to.on_surface() ? std::fabs(dot(to.n, d)) / std::sqrt(d2) : 1;
        return pdf_dir * c / d2;
    }

    // Area pdf of an emitter at v sending light to `to` (cosine emission about its normal).
    float pdf_light(const Vertex& v, const Vertex& to) const {
        Vec3 d = to.p - v.p;
        float c = dot(d, v.n_out) / length(d);
        float pd = mats[v.mat].two_sided ? std::fabs(c) * INV_PI * 0.5f : std::max(0.0f, c) * INV_PI;
        return convert(pd, v, to);
    }

    // Area pdf of choosing v as the light subpath's first vertex.
    float pdf_light_origin(const Vertex& v) const {
        if (!emissive(v) || v.shape == 2 || n_lights == 0) return 0;
        return 1 / (n_lights * v.area);
    }

    // Area pdf of sampling `next` from v, having arrived at v from `prev`.
    float pdf(const Vertex& v, const Vertex* prev, const Vertex& next) const {
        if (v.type == VType::Light) return pdf_light(v, next);
        Vec3 wn = next.p - v.p;
        float pd;
        if (v.type == VType::Camera) {
            pd = cam.pdf_dir(wn);
        } else {
            if (mats[v.mat].kind != Kind::Diffuse || dot(prev->p - v.p, v.n) * dot(wn, v.n) <= 0) return 0;
            pd = std::fabs(dot(wn, v.n)) / length(wn) * INV_PI;
        }
        return convert(pd, v, next);
    }

    bool visible(Vec3 a, Vec3 b) const {
        Vec3 d = b - a;
        float len = length(d);
        Hit occ;
        return !scene.intersect({a, d / len}, 1e-4f, len * (1 - 1e-3f), occ);
    }

    // Extend `path` from its last vertex. beta: throughput carried by `ray`; pdf_dir: its
    // solid-angle pdf. Escaped camera rays add beta * background to *escaped.
    void walk(std::vector<Vertex>& path, Ray ray, SampledSpectrum beta, float pdf_dir, SampledWavelengths& w,
              Rng& rng, const Spectrum* background, SampledSpectrum* escaped) const {
        for (int depth = 0; depth < MAX_DEPTH; ++depth) {
            Hit h;
            if (!scene.intersect(ray, 1e-4f, 1e30f, h)) {
                if (escaped) *escaped += beta * w.eval(*background);
                return;
            }
            Vertex v;
            v.p = h.p;
            v.n = h.n;
            v.n_out = h.front ? h.n : -h.n;
            v.mat = h.mat;
            v.shape = h.shape;
            v.area = h.area;
            v.beta = beta;
            Vertex& prev = path.back();
            v.pdf_fwd = convert(pdf_dir, prev, v);

            const Material& m = mats[h.mat];
            float pdf_rev_dir = 0;
            if (m.kind == Kind::Diffuse) {
                Vec3 d = normalize(ray.d), wi = sample_cosine_hemisphere(h.n, rng.next(), rng.next());
                pdf_dir = dot(wi, h.n) * INV_PI;
                pdf_rev_dir = -dot(d, h.n) * INV_PI;
                beta *= w.eval(m.albedo);
                ray = {h.p, wi};
            } else {
                v.delta = true;
                scatter(m, h, ray, w, beta, rng);  // may terminate secondary wavelengths
                pdf_dir = 0;
            }
            prev.pdf_rev = convert(pdf_rev_dir, v, prev);
            path.push_back(v);  // capacity reserved: `prev` stays valid until here
            if (!v.delta && pdf_dir <= 0) return;
            if (!roulette(depth, beta, rng)) return;
        }
    }

    // MIS weight of strategy (s, t) by the balance heuristic. Temporarily overwrites the
    // connection vertices' pdf_rev/delta and restores them.
    float mis_weight(int s, int t, std::vector<Vertex>& L, std::vector<Vertex>& C) const {
        if (s + t == 2) return 1;
        if (s == 0 && C[t - 1].shape == 2) return 1;  // emissive triangle: only s = 0 finds it
        Vertex *qs = s > 0 ? &L[s - 1] : nullptr, *pt = &C[t - 1];
        Vertex *qsm = s > 1 ? &L[s - 2] : nullptr, *ptm = t > 1 ? &C[t - 2] : nullptr;
        struct Saved { bool* d; bool dv; float* p; float pv; };
        Saved sv[6];
        int n = 0;
        auto set_pdf = [&](float& f, float v) { sv[n++] = {nullptr, false, &f, f}; f = v; };
        auto set_delta = [&](bool& d) { sv[n++] = {&d, d, nullptr, 0}; d = false; };

        float pt_rev = s > 0 ? pdf(*qs, qsm, *pt) : pdf_light_origin(*pt);
        float ptm_rev = ptm ? (s > 0 ? pdf(*pt, qs, *ptm) : pdf_light(*pt, *ptm)) : 0;
        float qs_rev = qs ? pdf(*pt, ptm, *qs) : 0;
        float qsm_rev = qsm ? pdf(*qs, pt, *qsm) : 0;
        set_delta(pt->delta);
        if (qs) set_delta(qs->delta);
        set_pdf(pt->pdf_rev, pt_rev);
        if (ptm) set_pdf(ptm->pdf_rev, ptm_rev);
        if (qs) set_pdf(qs->pdf_rev, qs_rev);
        if (qsm) set_pdf(qsm->pdf_rev, qsm_rev);

        auto remap = [](float f) { return f != 0 ? f : 1.0f; };
        float sum = 0, ri = 1;
        for (int i = t - 1; i > 0; --i) {
            ri *= remap(C[i].pdf_rev) / remap(C[i].pdf_fwd);
            if (!C[i].delta && !C[i - 1].delta) sum += ri;
        }
        ri = 1;
        for (int i = s - 1; i >= 0; --i) {
            ri *= remap(L[i].pdf_rev) / remap(L[i].pdf_fwd);
            if (!L[i].delta && !(i > 0 && L[i - 1].delta)) sum += ri;
        }
        while (n--) {
            if (sv[n].d) *sv[n].d = sv[n].dv;
            else *sv[n].p = sv[n].pv;
        }
        return 1 / (1 + sum);
    }

    // Unweighted contribution of strategy (s, t) times its MIS weight. t == 1 contributions
    // land on another pixel: splat is set and (u, v) is where.
    SampledSpectrum connect(int s, int t, std::vector<Vertex>& L, std::vector<Vertex>& C,
                            const SampledWavelengths& w, bool& splat, float& u, float& v) const {
        splat = false;
        SampledSpectrum r;
        if (s == 0) {
            const Vertex& pt = C[t - 1];
            if (pt.type != VType::Surface || !emissive(pt)) return r;
            r = pt.beta * Le(pt, C[t - 2].p, w);
        } else if (t == 1) {
            const Vertex& qs = L[s - 1];
            float ct;
            if (qs.delta || !cam.project(qs.p, u, v, ct)) return r;
            Vec3 to = cam.origin - qs.p;
            float d = length(to);
            SampledSpectrum fq = qs.type == VType::Light ? Le(qs, cam.origin, w) : f(qs, L[s - 2].p, cam.origin, w);
            float k = std::fabs(dot(qs.n, to)) / d * cam.importance(-to) * ct / (d * d);
            r = qs.beta * fq * k;
            if (r.v == SampledSpectrum{}.v || !visible(qs.p, cam.origin)) return SampledSpectrum{};
            splat = true;
        } else {
            const Vertex &qs = L[s - 1], &pt = C[t - 1];
            if (qs.delta || pt.delta) return r;
            SampledSpectrum fq = qs.type == VType::Light ? Le(qs, pt.p, w) : f(qs, L[s - 2].p, pt.p, w);
            SampledSpectrum fp = f(pt, C[t - 2].p, qs.p, w);
            Vec3 d = pt.p - qs.p;
            float d2 = dot(d, d), g = std::fabs(dot(qs.n, d)) * std::fabs(dot(pt.n, d)) / (d2 * d2);
            r = qs.beta * fq * fp * pt.beta * g;
            if (r.v == SampledSpectrum{}.v || !visible(qs.p, pt.p)) return SampledSpectrum{};
        }
        if (r.v == SampledSpectrum{}.v) return r;
        return r * mis_weight(s, t, L, C);
    }

    void camera_path(std::vector<Vertex>& C, Ray ray, SampledWavelengths& w, Rng& rng, const Spectrum& bg,
                     SampledSpectrum& escaped) const {
        C.clear();
        Vertex c0;
        c0.type = VType::Camera;
        c0.p = cam.origin;
        c0.n = cam.fwd;
        c0.beta.v.fill(1.0f);
        C.push_back(c0);
        SampledSpectrum one;
        one.v.fill(1.0f);
        walk(C, ray, one, cam.pdf_dir(ray.d), w, rng, &bg, &escaped);
    }

    void light_path(std::vector<Vertex>& L, SampledWavelengths& w, Rng& rng) const {
        L.clear();
        if (n_lights == 0) return;
        const auto& l = scene.lights[std::min(int(rng.next() * n_lights), int(n_lights) - 1)];
        Vertex v;
        v.type = VType::Light;
        float area;
        scene.sample_light(l, rng.next(), rng.next(), v.p, v.n_out, area, v.mat);
        v.area = area;
        v.shape = l.shape;
        v.pdf_fwd = 1 / (n_lights * area);
        v.beta.v.fill(1 / v.pdf_fwd);
        const Material& m = mats[v.mat];
        float sides = m.two_sided ? 2.0f : 1.0f;
        v.n = m.two_sided && rng.next() < 0.5f ? -v.n_out : v.n_out;
        Vec3 dir = sample_cosine_hemisphere(v.n, rng.next(), rng.next());
        float pdf_dir = dot(dir, v.n) * INV_PI / sides;
        // beta after emission: (1/pdf_pos) * Le * cos / pdf_dir = Le * pi * sides / pdf_pos
        SampledSpectrum beta = v.beta * w.eval(m.emission) * (m.emission_scale * std::numbers::pi_v<float> * sides);
        L.push_back(v);
        if (pdf_dir > 0) walk(L, {v.p, dir}, beta, pdf_dir, w, rng, nullptr, nullptr);
    }
};

// Full BDPT render. spp = camera (and light) subpaths per pixel.
inline Image render_bdpt(Scene scene, const std::vector<Material>& mats, const Spectrum& background,
                         const Camera& cam, int W, int H, int spp, float exposure, unsigned seed = 1,
                         unsigned threads = std::thread::hardware_concurrency()) {
    if (!scene.built()) scene.build();
    scene.collect_lights(mats);
    Bdpt b{scene, mats, cam, float(scene.lights.size())};
    threads = std::max(1u, threads);
    std::vector<XYZ> pix(size_t(W) * H, XYZ{0, 0, 0});
    std::vector<std::vector<XYZ>> splats(threads, std::vector<XYZ>(size_t(W) * H, XYZ{0, 0, 0}));
    std::atomic<int> next_row{0};
    std::vector<std::thread> pool;
    for (unsigned th = 0; th < threads; ++th)
        pool.emplace_back([&, th] {
            std::vector<Vertex> C, L;
            C.reserve(MAX_DEPTH + 2);
            L.reserve(MAX_DEPTH + 2);
            for (int y; (y = next_row++) < H;) {
                Rng rng(seed * 0x27D4EB2Du + unsigned(y));
                for (int x = 0; x < W; ++x) {
                    XYZ acc{0, 0, 0};
                    for (int s = 0; s < spp; ++s) {
                        auto w = SampledWavelengths::sample_uniform(rng.next());
                        SampledSpectrum Lp;
                        b.camera_path(C, cam.ray((x + rng.next()) / W, (y + rng.next()) / H), w, rng, background, Lp);
                        b.light_path(L, w, rng);
                        for (int t = 1; t <= int(C.size()); ++t)
                            for (int sl = 0; sl <= int(L.size()); ++sl) {
                                // (1, 1) would duplicate (0, 2): a light seen directly
                                if (t == 1 && sl <= 1) continue;
                                bool splat;
                                float u, v;
                                SampledSpectrum c = b.connect(sl, t, L, C, w, splat, u, v);
                                if (!splat) { Lp += c; continue; }
                                XYZ e = to_xyz(c, w);
                                clamp_luminance(e, scene.clamp);
                                XYZ& px = splats[th][size_t(std::min(int(v * H), H - 1)) * W + std::min(int(u * W), W - 1)];
                                for (int i = 0; i < 3; ++i) px[i] += e[i];
                            }
                        XYZ e = to_xyz(Lp, w);
                        clamp_luminance(e, scene.clamp);
                        for (int i = 0; i < 3; ++i) acc[i] += e[i];
                    }
                    pix[size_t(y) * W + x] = acc;
                }
            }
        });
    for (auto& t : pool) t.join();
    Image img(W, H);
    for (size_t p = 0; p < pix.size(); ++p) {
        XYZ sum = pix[p];
        for (auto& sp : splats) for (int i = 0; i < 3; ++i) sum[i] += sp[p][i];
        RGB c = xyz_to_linear_srgb(sum);
        for (int i = 0; i < 3; ++i) img.px[p][i] = c[i] * exposure / spp;
    }
    return img;
}
