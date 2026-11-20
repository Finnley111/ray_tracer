#pragma once
#include "camera.h"
#include "geometry.h"
#include "image.h"
#include "material.h"
#include "spectrum.h"
#include <algorithm>
#include <atomic>
#include <numbers>
#include <thread>
#include <vector>

constexpr int MAX_DEPTH = 64;
constexpr int RR_START = 3;

// Sample the next direction at a hit and update the path throughput. Shared by the
// camera path tracer and the light tracer. May terminate secondary wavelengths (dispersion).
inline void scatter(const Material& m, const Hit& h, Ray& ray, SampledWavelengths& w, SampledSpectrum& beta, Rng& rng) {
    if (m.kind == Kind::Diffuse) {
        // Lambertian with cosine sampling: f*cos/pdf = albedo.
        beta *= w.eval(m.albedo);
        ray = {h.p, sample_cosine_hemisphere(h.n, rng.next(), rng.next())};
        return;
    }
    Vec3 d = normalize(ray.d);
    if (m.kind == Kind::Dielectric) {
        // IOR varies across the 4 wavelengths -> only the hero can follow one direction.
        SampledSpectrum ior = w.eval(m.eta);
        if (!w.secondary_terminated() && (ior[0] != ior[1] || ior[0] != ior[2] || ior[0] != ior[3])) {
            w.terminate_secondary();
            for (int i = 1; i < N_LAMBDA; ++i) beta[i] = 0;
        }
        // Same direction for all surviving wavelengths, so use the hero's IOR.
        float eta = h.front ? ior[0] : 1 / ior[0], cos_i = -dot(d, h.n);
        float F = fresnel_conductor(cos_i, eta, 0);  // 1 under total internal reflection
        // Pick reflection with probability F: weight F/F = 1 either way.
        ray = {h.p, rng.next() < F ? reflect(d, h.n) : refract(d, h.n, eta)};
        return;
    }
    // Perfect specular reflection: weight is the reflectance.
    if (m.kind == Kind::Mirror) {
        beta *= w.eval(m.albedo);
    } else {
        float cos_i = -dot(d, h.n);
        for (int i = 0; i < N_LAMBDA; ++i) beta[i] *= fresnel_conductor(cos_i, m.eta(w.lambda[i]), m.k(w.lambda[i]));
    }
    ray = {h.p, reflect(d, h.n)};
}

// Russian roulette. Returns false if the path dies.
inline bool roulette(int depth, SampledSpectrum& beta, Rng& rng) {
    if (depth < RR_START) return true;
    float q = std::max(0.05f, 1 - *std::max_element(beta.v.begin(), beta.v.end()));
    if (rng.next() < q) return false;
    beta *= 1 / (1 - q);
    return true;
}

// Firefly clamp: scale c down so its luminance is at most max (0 = off). Keeps hue,
// loses energy (biased) — only the rare very bright samples are touched.
inline void clamp_luminance(XYZ& c, float max) {
    if (max <= 0 || c[1] <= max) return;
    float k = max / c[1];
    for (float& v : c) v *= k;
}

inline float power_heuristic(float a, float b) { return a * a / (a * a + b * b); }

// Path tracer with next-event estimation at diffuse vertices, combined with BSDF
// sampling by the power heuristic (MIS). NEE uses scene.lights; with none (or
// scene.use_nee false) it is plain BSDF sampling.
// Returns radiance at the path's 4 wavelengths. May terminate the secondary
// wavelengths in w (dispersion), so pass the same w on to to_xyz.
// skip_diffuse_first: stop after the first hit if it is diffuse (the light tracer
// covers those paths in render_hybrid); emission seen directly is still counted.
inline SampledSpectrum trace(const Scene& scene, const std::vector<Material>& mats, const Spectrum& background,
                             Ray ray, SampledWavelengths& w, Rng& rng, bool skip_diffuse_first = false) {
    constexpr float INV_PI = std::numbers::inv_pi_v<float>;
    const bool nee = scene.use_nee && !scene.lights.empty();
    const float n_lights = float(scene.lights.size());
    SampledSpectrum L, beta;
    beta.v.fill(1.0f);
    float prev_pdf_bsdf = 0;  // solid-angle pdf of the last diffuse bounce; 0 = camera/specular
    for (int depth = 0; depth < MAX_DEPTH; ++depth) {
        Hit h;
        if (!scene.intersect(ray, 1e-4f, 1e30f, h)) {
            L += beta * w.eval(background);
            break;
        }
        const Material& m = mats[h.mat];
        if (m.emission_scale > 0 && (h.front || m.two_sided)) {
            // BSDF-sampled hit on a light NEE could also have sampled: MIS weight it.
            float wt = 1;
            if (nee && prev_pdf_bsdf > 0 && h.shape != 2) {
                float len = length(ray.d), cos_l = std::fabs(dot(ray.d, h.n)) / len, d = h.t * len;
                wt = power_heuristic(prev_pdf_bsdf, d * d / (cos_l * h.area * n_lights));
            }
            L += beta * w.eval(m.emission) * (m.emission_scale * wt);
        }
        if (skip_diffuse_first && depth == 0 && m.kind == Kind::Diffuse) break;

        if (m.kind == Kind::Diffuse && nee) {
            // Next-event estimation: one uniformly chosen light, uniform point on it.
            const auto& l = scene.lights[std::min(int(rng.next() * n_lights), int(n_lights) - 1)];
            Vec3 p, ln;
            float area;
            int lmat;
            scene.sample_light(l, rng.next(), rng.next(), p, ln, area, lmat);
            Vec3 to = p - h.p;
            float d2 = dot(to, to), d = std::sqrt(d2);
            Vec3 wi = to / d;
            const Material& lm = mats[lmat];
            float cos_s = dot(wi, h.n), cos_l = -dot(wi, ln);
            if (lm.two_sided) cos_l = std::fabs(cos_l);
            Hit occ;
            if (cos_s > 0 && cos_l > 0 && !scene.intersect({h.p, wi}, 1e-4f, d * (1 - 1e-3f), occ)) {
                float pdf_l = d2 / (cos_l * area * n_lights), pdf_b = cos_s * INV_PI;
                L += beta * w.eval(m.albedo) * w.eval(lm.emission) *
                     (lm.emission_scale * INV_PI * cos_s * power_heuristic(pdf_l, pdf_b) / pdf_l);
            }
        }

        scatter(m, h, ray, w, beta, rng);
        prev_pdf_bsdf = m.kind == Kind::Diffuse ? std::max(0.0f, dot(ray.d, h.n)) * INV_PI : 0;
        if (!roulette(depth, beta, rng)) break;
    }
    return L;
}

inline void render_row(const Scene& scene, const std::vector<Material>& mats, const Spectrum& background,
                       const Camera& cam, Image& img, int y, int spp, float exposure, Rng& rng,
                       bool skip_diffuse_first = false) {
    for (int x = 0; x < img.w; ++x) {
        XYZ acc{0, 0, 0};
        for (int s = 0; s < spp; ++s) {
            auto w = SampledWavelengths::sample_uniform(rng.next());  // trace() may modify
            Ray r = cam.ray((x + rng.next()) / img.w, (y + rng.next()) / img.h);
            XYZ c = to_xyz(trace(scene, mats, background, r, w, rng, skip_diffuse_first), w);
            clamp_luminance(c, scene.clamp);
            for (int i = 0; i < 3; ++i) acc[i] += c[i];
        }
        RGB c = xyz_to_linear_srgb(acc);
        float k = exposure / spp;
        img.at(x, y) = {c[0] * k, c[1] * k, c[2] * k};
    }
}

// Takes the scene by value and builds its BVH, so callers never need to.
// Rows are handed out to threads through an atomic counter. Each row seeds its own
// Rng from (seed, y), so the image is identical whatever the thread count.
inline Image render(Scene scene, const std::vector<Material>& mats, const Spectrum& background,
                    const Camera& cam, int W, int H, int spp, float exposure, unsigned seed = 1,
                    unsigned threads = std::thread::hardware_concurrency(), bool skip_diffuse_first = false) {
    if (!scene.built()) scene.build();
    scene.collect_lights(mats);
    Image img(W, H);
    std::atomic<int> next_row{0};
    auto worker = [&] {
        for (int y; (y = next_row++) < H;) {
            Rng rng(seed * 0x9E3779B9u + unsigned(y));
            render_row(scene, mats, background, cam, img, y, spp, exposure, rng, skip_diffuse_first);
        }
    };
    std::vector<std::thread> pool;
    for (unsigned i = 0; i < std::max(1u, threads); ++i) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    return img;
}

// ---- Light tracing (particle tracing), for caustics such as the prism (RT-012) ----

// Trace one path from a random emissive quad. At every diffuse vertex, connect to the
// pinhole camera and splat into film (XYZ, W*H). n_paths = total paths over all threads.
// Emission is cosine-weighted about the front normal (two-sided lights: random side).
inline void light_path(const Scene& scene, const std::vector<Material>& mats, const std::vector<int>& lights,
                       const Camera& cam, int W, int H, double n_paths, std::vector<XYZ>& film, Rng& rng) {
    const Quad& L = scene.quads[lights[std::min(int(rng.next() * lights.size()), int(lights.size()) - 1)]];
    const Material& lm = mats[L.mat];
    Vec3 ln = cross(L.u, L.v);
    float area = length(ln);
    ln = ln / area;
    float sides = 1;
    if (lm.two_sided) {
        sides = 2;
        if (rng.next() < 0.5f) ln = -ln;
    }
    auto w = SampledWavelengths::sample_uniform(rng.next());
    // beta = Le * cos / (p_light * p_area * p_dir), p_dir = cos / (pi * sides)
    SampledSpectrum beta = w.eval(lm.emission) * (lm.emission_scale * area * lights.size() * sides * std::numbers::pi_v<float>);
    Ray ray{L.q + L.u * rng.next() + L.v * rng.next(), sample_cosine_hemisphere(ln, rng.next(), rng.next())};
    const float splat = float(W) * H / (n_paths * cam.film_area);

    for (int depth = 0; depth < MAX_DEPTH; ++depth) {
        Hit h;
        if (!scene.intersect(ray, 1e-4f, 1e30f, h)) break;
        const Material& m = mats[h.mat];
        if (m.kind == Kind::Diffuse) {
            Vec3 to = cam.origin - h.p;
            float d = length(to), cos_x = dot(to, h.n) / d, u, v, cos_c;
            Hit occ;
            if (cos_x > 0 && cam.project(h.p, u, v, cos_c) && !scene.intersect({h.p, to / d}, 1e-4f, d * (1 - 1e-4f), occ)) {
                // f * cos_x * We * cos_c / d^2, with pinhole We = 1 / (film_area * cos_c^4)
                float k = splat * cos_x / (std::numbers::pi_v<float> * d * d * cos_c * cos_c * cos_c);
                XYZ c = to_xyz(beta * w.eval(m.albedo) * k, w);
                clamp_luminance(c, scene.clamp * float(W) * H / float(n_paths));  // clamp as a per-pixel sample
                XYZ& px = film[size_t(std::min(int(v * H), H - 1)) * W + std::min(int(u * W), W - 1)];
                for (int i = 0; i < 3; ++i) px[i] += c[i];
            }
        }
        scatter(m, h, ray, w, beta, rng);
        if (!roulette(depth, beta, rng)) break;
    }
}

// Camera paths for everything not starting at a diffuse surface (lights seen directly,
// anything seen via glass/mirrors) + light paths for everything that does. The split is
// disjoint, so nothing is counted twice. Area-light quads only; no environment light.
inline Image render_hybrid(Scene scene, const std::vector<Material>& mats, const Camera& cam, int W, int H,
                           int spp, long n_light_paths, float exposure, unsigned seed = 1,
                           unsigned threads = std::thread::hardware_concurrency()) {
    if (!scene.built()) scene.build();
    Image img = render(scene, mats, constant(0.0f), cam, W, H, spp, exposure, seed, threads, true);
    std::vector<int> lights;
    for (int i = 0; i < int(scene.quads.size()); ++i)
        if (mats[scene.quads[i].mat].emission_scale > 0) lights.push_back(i);
    if (lights.empty()) return img;

    const long CHUNK = 1 << 16;
    std::atomic<long> next{0};
    threads = std::max(1u, threads);
    std::vector<std::vector<XYZ>> films(threads, std::vector<XYZ>(size_t(W) * H, XYZ{0, 0, 0}));
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t)
        pool.emplace_back([&, t] {
            for (long c; (c = next++) * CHUNK < n_light_paths;) {
                Rng rng(seed * 0x85EBCA6Bu + unsigned(c));
                for (long i = c * CHUNK; i < std::min(n_light_paths, (c + 1) * CHUNK); ++i)
                    light_path(scene, mats, lights, cam, W, H, double(n_light_paths), films[t], rng);
            }
        });
    for (auto& t : pool) t.join();
    for (size_t p = 0; p < img.px.size(); ++p) {
        XYZ sum{0, 0, 0};
        for (auto& f : films) for (int i = 0; i < 3; ++i) sum[i] += f[p][i];
        RGB c = xyz_to_linear_srgb(sum);
        for (int i = 0; i < 3; ++i) img.px[p][i] += c[i] * exposure;
    }
    return img;
}
