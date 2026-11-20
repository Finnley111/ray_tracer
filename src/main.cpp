#include "camera.h"
#include "color.h"
#include "geometry.h"
#include "image.h"
#include "integrator.h"
#include "material.h"
#include "metals.h"
#include "obj.h"
#include "scene_file.h"
#include "spectrum.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <string>

static Rng rng(1);

// Blackbody swatch, 1000 K (left) -> 12000 K (right). Top: MC over hero
// wavelengths; bottom: deterministic integral. Columns normalised to max channel 1.
static Image swatch() {
    const int SPP = 64;
    Image img(800, 200);
    for (int x = 0; x < img.w; ++x) {
        Spectrum bb = blackbody(1000.0f + 11000.0f * x / (img.w - 1));
        RGB ref = xyz_to_linear_srgb(spectrum_to_xyz(bb));
        float scale = 1.0f / std::max({ref[0], ref[1], ref[2]});
        for (int y = 0; y < img.h; ++y) {
            RGB c = ref;
            if (y < img.h / 2) {
                XYZ acc{0, 0, 0};
                for (int s = 0; s < SPP; ++s) {
                    auto w = SampledWavelengths::sample_uniform(rng.next());
                    XYZ e = to_xyz(w.eval(bb), w);
                    for (int i = 0; i < 3; ++i) acc[i] += e[i] / SPP;
                }
                c = xyz_to_linear_srgb(acc);
            }
            img.at(x, y) = {c[0] * scale, c[1] * scale, c[2] * scale};
        }
    }
    return img;
}

// Geometry debug view: colour = (normal + 1) / 2, background black.
static Image normals() {
    const int W = 640, H = 360, SPP = 4;
    Scene scene;
    scene.spheres = {{{0, 1, 0}, 1, 0}, {{-2.2f, 0.6f, 0.5f}, 0.6f, 0}, {{2.0f, 0.8f, -0.8f}, 0.8f, 0}};
    scene.quads = {{{-6, 0, -6}, {0, 0, 12}, {12, 0, 0}, 0}};  // floor, u x v = +y
    Camera cam({0, 2, 6}, {0, 0.8f, 0}, {0, 1, 0}, 40, float(W) / H);
    Image img(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            Vec3 acc;
            for (int s = 0; s < SPP; ++s) {
                Hit h;
                Ray r = cam.ray((x + rng.next()) / W, (y + rng.next()) / H);
                if (scene.intersect(r, 1e-4f, 1e30f, h)) acc += (h.n + Vec3{1, 1, 1}) * (0.5f / SPP);
            }
            img.at(x, y) = {acc.x, acc.y, acc.z};
        }
    return img;
}

// RT-008: diffuse scene lit only by a 3000 K blackbody panel overhead.
// Neutral (constant-albedo) floor should come out warm; the coloured
// spheres use Gaussian reflectance spectra, not RGB.
static Image lights() {
    const int W = 480, H = 270, SPP = 128;
    std::vector<Material> mats = {
        {constant(0.8f)},                                           // 0 white
        {[](float l) { return 0.05f + 0.8f * gaussian(620, 40)(l); }},  // 1 red
        {[](float l) { return 0.05f + 0.7f * gaussian(540, 30)(l); }},  // 2 green
        {[](float l) { return 0.05f + 0.7f * gaussian(460, 30)(l); }},  // 3 blue
        {constant(0.0f), blackbody(3000), 25.0f},                   // 4 light
    };
    Scene scene;
    scene.spheres = {{{0, 1, 0}, 1, 1}, {{-2.2f, 0.6f, 0.5f}, 0.6f, 2}, {{2.0f, 0.8f, -0.8f}, 0.8f, 3}};
    scene.quads = {{{-6, 0, -6}, {0, 0, 12}, {12, 0, 0}, 0},   // floor
                   {{-1, 4, -1}, {2, 0, 0}, {0, 0, 2}, 4}};    // light
    Camera cam({0, 2, 6}, {0, 0.8f, 0}, {0, 1, 0}, 40, float(W) / H);
    return render(scene, mats, constant(0.0f), cam, W, H, SPP, 1.0f);
}

// Classic 555-unit Cornell room: walls + downward ceiling light (one-sided, u x v = -y).
static void add_cornell_room(Scene& sc, int white, int red, int green, int light) {
    sc.quads.insert(sc.quads.end(), {
        {{555, 0, 0}, {0, 555, 0}, {0, 0, 555}, red},          // left (camera's left is +x)
        {{0, 0, 0}, {0, 555, 0}, {0, 0, 555}, green},          // right
        {{0, 0, 0}, {555, 0, 0}, {0, 0, 555}, white},          // floor
        {{0, 555, 0}, {555, 0, 0}, {0, 0, 555}, white},        // ceiling
        {{0, 0, 555}, {555, 0, 0}, {0, 555, 0}, white},        // back
        {{343, 554, 332}, {-130, 0, 0}, {0, 0, -105}, light},  // light
    });
}
static void add_rot_box(Scene& sc, Vec3 p, Vec3 size, float deg, int mat) {
    float r = deg * std::numbers::pi_v<float> / 180, c = std::cos(r), s = std::sin(r);
    add_box(sc.quads, p, Vec3{c, 0, -s} * size.x, {0, size.y, 0}, Vec3{s, 0, c} * size.z, mat);
}
static Camera cornell_camera() { return Camera({278, 278, -800}, {278, 278, 0}, {0, 1, 0}, 40, 1.0f); }

// RT-009: Cornell box (classic 555-unit dimensions), D65 area light.
// Wall reflectances are smooth spectral curves, not RGB triples.
static Image cornell(int spp, bool nee = true, unsigned seed = 1) {
    const int W = 400, H = 400;
    auto red = [](float l) { return 0.05f + 0.6f / (1 + std::exp(-(l - 600) / 12)); };
    std::vector<Material> mats = {
        {constant(0.73f)},                                              // 0 white
        {red},                                                          // 1 red
        {[](float l) { return 0.05f + 0.45f * gaussian(530, 35)(l); }},  // 2 green
        {constant(0.0f), d65(), 15.0f},                                 // 3 light
    };
    Scene sc;
    add_cornell_room(sc, 0, 1, 2, 3);
    add_rot_box(sc, {265, 0, 295}, {165, 330, 165}, 15, 0);   // tall
    add_rot_box(sc, {130, 0, 65}, {165, 165, 165}, -18, 0);   // short
    sc.use_nee = nee;
    Camera cam = cornell_camera();
    return render(sc, mats, constant(0.0f), cam, W, H, spp, 1.0f, seed);
}

// RT-010: gold, copper, silver (measured n,k) and a 95% mirror under a D65 panel
// and a dim D65 sky. Metal colours come only from the spectral Fresnel term.
static Image metals(int spp) {
    const int W = 640, H = 300;
    std::vector<Material> mats = {
        {constant(0.73f)},                   // 0 floor
        conductor(au_eta(), au_k()),         // 1 gold
        conductor(cu_eta(), cu_k()),         // 2 copper
        conductor(ag_eta(), ag_k()),         // 3 silver
        mirror(constant(0.95f)),             // 4 mirror
        {constant(0.0f), d65(), 6.0f},       // 5 light
    };
    Scene sc;
    sc.spheres = {{{-3.3f, 1, 0}, 1, 1}, {{-1.1f, 1, 0}, 1, 2}, {{1.1f, 1, 0}, 1, 3}, {{3.3f, 1, 0}, 1, 4}};
    sc.quads = {{{-20, 0, -20}, {0, 0, 40}, {40, 0, 0}, 0},
                {{-3, 6, -1}, {6, 0, 0}, {0, 0, 2}, 5}};
    Spectrum s = d65();
    Spectrum sky = [s](float l) { return 0.4f * s(l); };
    Camera cam({0, 2.2f, 8}, {0, 1, 0}, {0, 1, 0}, 35, float(W) / H);
    return render(sc, mats, sky, cam, W, H, spp, 1.0f);
}

// RT-011: BK7, SF11 (dense flint) and an exaggerated-dispersion Cauchy glass
// over a black/white striped floor so refracted edges show colour fringes.
static Image glass(int spp) {
    const int W = 640, H = 300;
    std::vector<Material> mats = {
        {constant(0.8f)},                 // 0 white stripe
        {constant(0.03f)},                // 1 black stripe
        dielectric(glass_bk7()),          // 2
        dielectric(glass_sf11()),         // 3
        dielectric(cauchy(1.5f, 0.06f)),  // 4 ~15x BK7's dispersion
        {constant(0.0f), d65(), 6.0f},    // 5 light
    };
    Scene sc;
    for (int i = 0; i < 40; ++i)  // stripes run toward the camera
        sc.quads.push_back({{-10.0f + 0.5f * i, 0, -20}, {0, 0, 40}, {0.5f, 0, 0}, i % 2});
    sc.quads.push_back({{-3, 6, -1}, {6, 0, 0}, {0, 0, 2}, 5});
    sc.spheres = {{{-2.4f, 1, 0}, 1, 2}, {{0, 1, 0}, 1, 3}, {{2.4f, 1, 0}, 1, 4}};
    Spectrum s = d65();
    Spectrum sky = [s](float l) { return 0.3f * s(l); };
    Camera cam({0, 2.2f, 7}, {0, 0.9f, 0}, {0, 1, 0}, 38, float(W) / H);
    return render(sc, mats, sky, cam, W, H, spp, 1.0f);
}

// RT-012: light behind a slit -> equilateral prism -> white screen. The beam path is
// computed for 550 nm at minimum deviation, so the slit, light and screen follow the glass.
// The rainbow is a caustic, so this uses render_hybrid (light tracing); spp here is light
// paths per pixel.
static SceneDesc prism_desc(bool exaggerated) {
    const int W = 480, H = 270;
    Spectrum ior = exaggerated ? cauchy(1.5f, 0.05f) : glass_sf11();
    std::vector<Material> mats = {
        {constant(0.8f)},                   // 0 screen
        {constant(0.02f)},                  // 1 slit wall (black)
        dielectric(ior),                    // 2 prism
        {constant(0.0f), d65(), 1500.0f},   // 3 light
        {constant(0.15f)},                  // 4 floor
    };
    Scene sc;
    const float S = 1.2f, PH = 1.4f, Ht = S * std::sqrt(3.0f) / 2;
    const Vec3 up{0, PH, 0}, A{0, 0, 2 * Ht / 3}, B1{-S / 2, 0, -Ht / 3}, B2{S / 2, 0, -Ht / 3};
    sc.tris = {{A, B1, B2, 2}, {A + up, B1 + up, B2 + up, 2}};
    for (auto [p, q] : {std::pair{A, B1}, {B1, B2}, {B2, A}})
        sc.tris.insert(sc.tris.end(), {{p, q, q + up, 2}, {p, q + up, p + up, 2}});
    orient_convex(sc.tris);

    // Inside the prism the minimum-deviation ray runs parallel to the base (+x).
    // Outward face normals are horizontal; refract the inside ray out of each face.
    auto outward = [](Vec3 p, Vec3 q, Vec3 opp) {
        Vec3 n = normalize(cross(q - p, Vec3{0, 1, 0}));
        return dot(n, p - opp) > 0 ? n : -n;
    };
    Vec3 nL = outward(A, B1, B2), nR = outward(A, B2, B1);
    Vec3 E = (A + B1) / 2 + up / 2, X = (A + B2) / 2 + up / 2;  // entry, exit points
    auto exit_dir = [&](float l) { return refract({1, 0, 0}, -nR, 1 / ior(l)); };
    Vec3 d_in = -refract({-1, 0, 0}, -nL, 1 / ior(550)), d_out = exit_dir(550);
    std::fprintf(stderr, "prism: n(550)=%.4f deviation %.1f deg, 450-650 nm spread %.2f deg\n", ior(550),
                 std::acos(dot(d_in, d_out)) * 180 / std::numbers::pi_v<float>,
                 std::acos(dot(exit_dir(450), exit_dir(650))) * 180 / std::numbers::pi_v<float>);

    // Slit wall perpendicular to the incoming beam, 0.6 before the prism; 4 quads around the gap.
    const Vec3 Y{0, 1, 0};
    Vec3 side = normalize(cross(Y, d_in)), C = E - d_in * 0.6f;
    C.y = 0;
    const float SW = 0.12f, WALL = 3, WH = 2.5f, y0 = 0.35f, y1 = 1.05f;
    sc.quads = {{C + side * (SW / 2), side * (WALL - SW / 2), Y * WH, 1},
                {C - side * WALL, side * (WALL - SW / 2), Y * WH, 1},
                {C - side * (SW / 2), side * SW, Y * y0, 1},
                {C - side * (SW / 2) + Y * y1, side * SW, Y * (WH - y1), 1}};
    // Light: 4 cm wide strip 4 units behind the slit -> ~2.3 deg horizontal divergence.
    Vec3 Lc = C - d_in * 4.0f;
    sc.quads.push_back({Lc - side * 0.02f + Y * 0.2f, side * 0.04f, Y * 1.0f, 3});
    // Screen: 4 wide, 4 units down the 550 nm exit ray, facing back at the prism.
    Vec3 sside = normalize(cross(Y, d_out)), Sc = X + d_out * 4.0f;
    Sc.y = 0;
    sc.quads.push_back({Sc - sside * 2.0f, sside * 4.0f, Y * 2.5f, 0});
    sc.quads.push_back({{-20, 0, -20}, {0, 0, 40}, {40, 0, 0}, 4});  // floor

    // Dim overhead fill light so the prism and screen are visible.
    mats.push_back({constant(0.0f), d65(), 0.25f});
    sc.quads.push_back({X + Y * 4.0f - Vec3{2, 0, 2}, {4, 0, 0}, {0, 0, 4}, 5});
    // Camera beside the beam, up and back from the prism, looking at the screen.
    Vec3 look = X + d_out * 2.8f;
    look.y = 0.6f;
    SceneDesc d;
    d.scene = sc;
    d.mats = mats;
    d.cam = Camera(X - d_out * 1.5f + sside * -2.5f + Y * 2.5f, look, Y, 55, float(W) / H);
    d.W = W;
    d.H = H;
    d.spp = 256;  // camera paths
    d.hybrid = true;
    d.light_paths = 4096;
    return d;
}
static Image prism(int spp, bool exaggerated) { return render_desc(prism_desc(exaggerated), spp); }

// RT-013: ~10k-triangle UV sphere + floor. Renders with and without the BVH,
// prints both times and the largest pixel difference.
static Image bvhbench() {
    const int W = 160, H = 90, SPP = 4, NU = 100, NV = 50;
    std::vector<Material> mats = {{constant(0.7f)}, {constant(0.0f), d65(), 3.0f}};
    Scene sc;
    auto P = [&](int i, int j) {
        float th = std::numbers::pi_v<float> * j / NV, ph = 2 * std::numbers::pi_v<float> * i / NU;
        return Vec3{std::sin(th) * std::cos(ph), 1 + std::cos(th), std::sin(th) * std::sin(ph)};
    };
    for (int i = 0; i < NU; ++i)
        for (int j = 0; j < NV; ++j) {
            Vec3 a = P(i, j), b = P(i + 1, j), c = P(i + 1, j + 1), d = P(i, j + 1);
            sc.tris.insert(sc.tris.end(), {{a, b, c, 0}, {a, c, d, 0}});
        }
    sc.quads = {{{-6, 0, -6}, {0, 0, 12}, {12, 0, 0}, 0}, {{-1, 4, -1}, {2, 0, 0}, {0, 0, 2}, 1}};
    Camera cam({0, 2, 5}, {0, 1, 0}, {0, 1, 0}, 40, float(W) / H);
    auto timed = [&](const Scene& s, double& secs) {
        auto t0 = std::chrono::steady_clock::now();
        Image img = render(s, mats, constant(0.2f), cam, W, H, SPP, 1.0f);
        secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return img;
    };
    // Linear: hand render() a scene whose "BVH" is one leaf holding every primitive.
    Scene flat = sc;
    flat.build();
    flat.nodes.resize(1);
    flat.nodes[0].count = int(flat.prims.size());
    flat.nodes[0].start = 0;
    double t_lin, t_bvh;
    Image a = timed(flat, t_lin), b = timed(sc, t_bvh);
    float diff = 0;
    for (size_t i = 0; i < a.px.size(); ++i)
        for (int k = 0; k < 3; ++k) diff = std::max(diff, std::fabs(a.px[i][k] - b.px[i][k]));
    std::fprintf(stderr, "%zu triangles: linear %.2fs, BVH %.3fs (%.0fx), max pixel diff %g\n", sc.tris.size(),
                 t_lin, t_bvh, t_lin / t_bvh, diff);
    return b;
}

// RT-015: Stanford bunny (assets/bunny.obj, see CLAUDE.md) in BK7 glass, with a gold
// bunny beside it, on a striped floor.
static Image bunny(int spp) {
    const int W = 640, H = 360;
    std::vector<Material> mats = {
        {constant(0.8f)}, {constant(0.03f)},     // 0, 1 stripes
        dielectric(glass_bk7()),                 // 2 glass
        conductor(au_eta(), au_k()),             // 3 gold
        {constant(0.0f), d65(), 6.0f},           // 4 light
    };
    Scene sc;
    std::string err;
    std::vector<Triangle> glass_b, gold_b;
    if (!load_obj("assets/bunny.obj", 2, glass_b, err) || !load_obj("assets/bunny.obj", 3, gold_b, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return Image(0, 0);
    }
    fit_mesh(glass_b, 2.0f, -1.3f, 0, 0);
    fit_mesh(gold_b, 2.0f, 1.5f, -0.5f, 0);
    double vol = 0;  // signed volume > 0 means faces wind outward (matters for glass)
    for (auto& t : glass_b) vol += dot(t.a, cross(t.b, t.c)) / 6;
    std::fprintf(stderr, "bunny: %zu triangles each, signed volume %.3f\n", glass_b.size(), vol);
    sc.tris = glass_b;
    sc.tris.insert(sc.tris.end(), gold_b.begin(), gold_b.end());
    for (int i = 0; i < 40; ++i) sc.quads.push_back({{-10.0f + 0.5f * i, 0, -20}, {0, 0, 40}, {0.5f, 0, 0}, i % 2});
    sc.quads.push_back({{-3, 6, -1}, {6, 0, 0}, {0, 0, 2}, 4});
    Spectrum s = d65();
    Camera cam({0, 2.2f, 6.5f}, {0, 0.9f, 0}, {0, 1, 0}, 40, float(W) / H);
    return render(sc, mats, [s](float l) { return 0.3f * s(l); }, cam, W, H, spp, 1.0f);
}

// RT-016: Cornell box noise, NEE+MIS at 64 spp vs BSDF-only at 1024 spp. Noise is the
// RMS difference between two independent renders / sqrt(2) (= per-image RMS error),
// on display values clamped to [0,1].
static Image neebench() {
    auto stats = [](const Image& a, const Image& b, double& mean, double& noise) {
        double m = 0, e = 0;
        for (size_t i = 0; i < a.px.size(); ++i)
            for (int k = 0; k < 3; ++k) {
                // Display-referred: clamp to [0,1] so the light's anti-aliased edges
                // (~100x brighter than the room) don't swamp the lighting noise.
                double x = std::clamp(a.px[i][k], 0.0f, 1.0f), y = std::clamp(b.px[i][k], 0.0f, 1.0f);
                m += (x + y) / 2;
                double d = x - y;
                e += d * d;
            }
        mean = m / (3.0 * a.px.size());
        noise = std::sqrt(e / (3.0 * a.px.size()) / 2);
    };
    auto timed = [](auto f, double& t) {
        auto t0 = std::chrono::steady_clock::now();
        Image img = f();
        t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return img;
    };
    double tn, tb, mn, nn, mb, nb;
    Image n1 = timed([] { return cornell(64, true, 1); }, tn), n2 = cornell(64, true, 2);
    Image b1 = timed([] { return cornell(1024, false, 1); }, tb), b2 = cornell(1024, false, 2);
    stats(n1, n2, mn, nn);
    stats(b1, b2, mb, nb);
    std::fprintf(stderr, "NEE+MIS   64 spp: %5.1fs  mean %.4f  noise %.4f\n", tn, mn, nn);
    std::fprintf(stderr, "BSDF    1024 spp: %5.1fs  mean %.4f  noise %.4f\n", tb, mb, nb);
    std::fprintf(stderr, "noise ratio %.2fx lower with NEE at 1/16 the samples; means differ %.2f%%\n", nb / nn,
                 100 * std::fabs(mn - mb) / mb);
    write_ppm(b1, "renders/cornell_bsdf1024.ppm");
    return n1;
}

// RT-017: showcase. Cornell room with RGB-authored walls (Smits upsampling), a 3000 K
// blackbody ceiling light, an SF11 glass prism and a gold sphere (measured n,k).
// hybrid: light tracing for diffuse-first paths, so the prism/gold caustics on the
// floor converge; spp is then light paths per pixel (camera paths fixed at 512).
// bdpt: bidirectional path tracing (RT-019); spp = samples per pixel.
static SceneDesc showcase_desc() {
    const int W = 500, H = 500;
    std::vector<Material> mats = {
        {rgb_spectrum(0.73f, 0.73f, 0.73f)},         // 0 white
        {rgb_spectrum(0.65f, 0.05f, 0.05f)},         // 1 red
        {rgb_spectrum(0.12f, 0.45f, 0.15f)},         // 2 green
        {constant(0.0f), blackbody(3000), 40.0f},    // 3 light
        dielectric(glass_sf11()),                    // 4 prism
        conductor(au_eta(), au_k()),                 // 5 gold
    };
    Scene sc;
    add_cornell_room(sc, 0, 1, 2, 3);
    add_rot_box(sc, {265, 0, 295}, {165, 330, 165}, 15, 0);  // tall box, back left
    sc.spheres = {{{150, 90, 190}, 90, 5}};                   // gold sphere, front right
    // Equilateral SF11 prism standing front left, rotated 20 degrees.
    const float S = 150, PH = 230, Ht = S * std::sqrt(3.0f) / 2, a = 20 * std::numbers::pi_v<float> / 180;
    auto rot = [&](float x, float z) { return Vec3{390 + x * std::cos(a) - z * std::sin(a), 0, 140 + x * std::sin(a) + z * std::cos(a)}; };
    Vec3 A = rot(0, 2 * Ht / 3), B1 = rot(-S / 2, -Ht / 3), B2 = rot(S / 2, -Ht / 3), up{0, PH, 0};
    sc.tris = {{A, B1, B2, 4}, {A + up, B1 + up, B2 + up, 4}};
    for (auto [p, q] : {std::pair{A, B1}, {B1, B2}, {B2, A}})
        sc.tris.insert(sc.tris.end(), {{p, q, q + up, 4}, {p, q + up, p + up, 4}});
    orient_convex(sc.tris);
    SceneDesc d;
    d.scene = sc;
    d.mats = mats;
    d.cam = cornell_camera();
    d.W = W;
    d.H = H;
    d.spp = 512;  // camera paths (hybrid) / samples (pt)
    d.light_paths = 1024;
    return d;
}
enum class Mode { PT, Hybrid, BDPT };
static Image showcase(Mode m, int spp) {
    SceneDesc d = showcase_desc();
    d.scene.clamp = 5;  // firefly clamp; ~0.1% darker overall (bdptbench stays unclamped)
    d.hybrid = m == Mode::Hybrid;
    d.bdpt = m == Mode::BDPT;
    if (m == Mode::PT) d.spp = spp;
    return render_desc(d, m == Mode::PT ? 0 : spp);
}

// RT-019 acceptance numbers.
// (a) Prism: BDPT vs hybrid mean over the screen's bounding box (the caustic).
// (b) Showcase at equal wall-clock time: BDPT vs hybrid two-seed noise (display-clamped,
//     as neebench) over the prism's and the gold sphere's screen bounding boxes, and overall.
static Image bdptbench() {
    using clk = std::chrono::steady_clock;
    auto secs = [](clk::time_point t0) { return std::chrono::duration<double>(clk::now() - t0).count(); };
    struct Box { int x0, y0, x1, y1; };
    auto bbox = [](const SceneDesc& d, std::initializer_list<Vec3> pts) {
        Box b{d.W, d.H, 0, 0};
        for (Vec3 p : pts) {
            float u, v, c;
            if (!d.cam.project(p, u, v, c)) continue;
            int x = int(u * d.W), y = int(v * d.H);
            b = {std::min(b.x0, x), std::min(b.y0, y), std::max(b.x1, x + 1), std::max(b.y1, y + 1)};
        }
        return b;
    };
    auto mean = [](const Image& im, Box b) {
        double m = 0;
        for (int y = b.y0; y < b.y1; ++y)
            for (int x = b.x0; x < b.x1; ++x) for (float c : im.px[size_t(y) * im.w + x]) m += c;
        return m / (3.0 * (b.x1 - b.x0) * (b.y1 - b.y0));
    };
    auto noise = [](const Image& a, const Image& b, Box r) {
        double e = 0;
        for (int y = r.y0; y < r.y1; ++y)
            for (int x = r.x0; x < r.x1; ++x)
                for (int k = 0; k < 3; ++k) {
                    size_t i = size_t(y) * a.w + x;
                    double d = std::clamp(a.px[i][k], 0.0f, 1.0f) - std::clamp(b.px[i][k], 0.0f, 1.0f);
                    e += d * d;
                }
        return std::sqrt(e / (3.0 * (r.x1 - r.x0) * (r.y1 - r.y0)) / 2);
    };

    {  // (a) prism caustic
        SceneDesc d = prism_desc(false);
        const Quad* scr = nullptr;
        for (auto& q : d.scene.quads) if (q.mat == 0) scr = &q;
        Box b = bbox(d, {scr->q, scr->q + scr->u, scr->q + scr->v, scr->q + scr->u + scr->v});
        auto t0 = clk::now();
        double mh = mean(render_desc(d, 1024), b);
        double th = secs(t0);
        d.hybrid = false;
        d.bdpt = true;
        t0 = clk::now();
        double mb = mean(render_desc(d, 64), b);
        std::fprintf(stderr, "prism screen mean: hybrid %.5f (%.0fs)  bdpt %.5f (%.0fs)  diff %.2f%%\n", mh, th, mb,
                     secs(t0), 100 * std::fabs(mb - mh) / mh);
    }

    // (b) showcase, equal time
    SceneDesc h = showcase_desc(), bd = showcase_desc();
    h.hybrid = true;
    h.spp = 64;
    h.light_paths = 128;
    bd.bdpt = true;
    const Sphere& gold = h.scene.spheres[0];
    Vec3 gr{gold.r, gold.r, gold.r};
    Box gbox = bbox(h, {gold.c - gr, gold.c + gr, gold.c + Vec3{gr.x, -gr.y, gr.z}, gold.c + Vec3{-gr.x, gr.y, -gr.z},
                        gold.c + Vec3{gr.x, gr.y, -gr.z}, gold.c + Vec3{-gr.x, -gr.y, gr.z}, gold.c + Vec3{gr.x, -gr.y, -gr.z},
                        gold.c + Vec3{-gr.x, gr.y, gr.z}});
    Box pbox{h.W, h.H, 0, 0};
    for (auto& t : h.scene.tris) {
        Box tb = bbox(h, {t.a, t.b, t.c});
        pbox = {std::min(pbox.x0, tb.x0), std::min(pbox.y0, tb.y0), std::max(pbox.x1, tb.x1), std::max(pbox.y1, tb.y1)};
    }
    Box all{0, 0, h.W, h.H};
    auto t0 = clk::now();
    Image h1 = render_desc(h, 0, 1), h2 = render_desc(h, 0, 2);
    double th = secs(t0) / 2;
    t0 = clk::now();
    render_desc(bd, 4, 9);
    int spp_b = std::max(1, int(4 * th / secs(t0) + 0.5));
    t0 = clk::now();
    Image b1 = render_desc(bd, spp_b, 1), b2 = render_desc(bd, spp_b, 2);
    double tb = secs(t0) / 2;
    std::fprintf(stderr, "showcase equal time: hybrid 64 cam + 128 light ppp %.1fs | bdpt %d spp %.1fs\n", th, spp_b, tb);
    std::fprintf(stderr, "  noise   prism %.4f -> %.4f (%.2fx)   gold %.4f -> %.4f (%.2fx)   all %.4f -> %.4f (%.2fx)\n",
                 noise(h1, h2, pbox), noise(b1, b2, pbox), noise(h1, h2, pbox) / noise(b1, b2, pbox), noise(h1, h2, gbox),
                 noise(b1, b2, gbox), noise(h1, h2, gbox) / noise(b1, b2, gbox), noise(h1, h2, all), noise(b1, b2, all),
                 noise(h1, h2, all) / noise(b1, b2, all));
    std::fprintf(stderr, "  mean all: hybrid %.4f  bdpt %.4f\n", (mean(h1, all) + mean(h2, all)) / 2, (mean(b1, all) + mean(b2, all)) / 2);
    write_image(h1, "renders/bench_hybrid.png");
    return b1;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: rt <scene.txt | swatch|normals|lights|cornell|metals|glass|prism|prism-x|bvhbench|bunny|neebench|showcase|showcase-pt|showcase-bdpt|bdptbench> <out.png|out.ppm> [spp]\n"
                     "  [spp] overrides the scene's spp (pt, bdpt) or light paths per pixel (hybrid)\n");
        return 2;
    }
    std::string mode = argv[1];
    int spp_arg = argc > 3 ? std::atoi(argv[3]) : 0;
    if (argc > 3 && spp_arg <= 0) {
        std::fprintf(stderr, "spp must be a positive integer\n");
        return 2;
    }
    if (mode.find('.') != std::string::npos) {  // a scene file, e.g. scenes/example.txt
        SceneDesc d;
        std::string err;
        if (!load_scene(mode, d, err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 2;
        }
        Image img = render_desc(d, spp_arg);
        if (!write_image(img, argv[2])) {
            std::fprintf(stderr, "could not write %s\n", argv[2]);
            return 1;
        }
        std::printf("wrote %s\n", argv[2]);
        return 0;
    }
    Image img = mode == "swatch" ? swatch() : mode == "normals" ? normals()
                : mode == "lights"  ? lights()
                : mode == "cornell" ? cornell(argc > 3 ? std::atoi(argv[3]) : 1024)
                : mode == "metals"  ? metals(argc > 3 ? std::atoi(argv[3]) : 256)
                : mode == "glass"   ? glass(argc > 3 ? std::atoi(argv[3]) : 512)
                : mode == "prism"   ? prism(argc > 3 ? std::atoi(argv[3]) : 4096, false)
                : mode == "prism-x" ? prism(argc > 3 ? std::atoi(argv[3]) : 4096, true)
                : mode == "bvhbench" ? bvhbench()
                : mode == "bunny"   ? bunny(argc > 3 ? std::atoi(argv[3]) : 256)
                : mode == "neebench" ? neebench()
                : mode == "showcase" ? showcase(Mode::Hybrid, argc > 3 ? std::atoi(argv[3]) : 1024)
                : mode == "showcase-pt" ? showcase(Mode::PT, argc > 3 ? std::atoi(argv[3]) : 1024)
                : mode == "showcase-bdpt" ? showcase(Mode::BDPT, argc > 3 ? std::atoi(argv[3]) : 256)
                : mode == "bdptbench" ? bdptbench()
                                    : Image(0, 0);
    if (img.w == 0) {
        std::fprintf(stderr, "unknown mode %s (or scene failed to load)\n", argv[1]);
        return 2;
    }
    if (!write_image(img, argv[2])) {
        std::fprintf(stderr, "could not write %s\n", argv[2]);
        return 1;
    }
    std::printf("wrote %s\n", argv[2]);
}
