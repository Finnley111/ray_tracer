#include "camera.h"
#include "color.h"
#include "geometry.h"
#include "image.h"
#include "integrator.h"
#include "material.h"
#include "metals.h"
#include "spectrum.h"
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

// RT-009: Cornell box (classic 555-unit dimensions), D65 area light.
// Wall reflectances are smooth spectral curves, not RGB triples.
static Image cornell(int spp) {
    const int W = 400, H = 400;
    auto red = [](float l) { return 0.05f + 0.6f / (1 + std::exp(-(l - 600) / 12)); };
    std::vector<Material> mats = {
        {constant(0.73f)},                                              // 0 white
        {red},                                                          // 1 red
        {[](float l) { return 0.05f + 0.45f * gaussian(530, 35)(l); }},  // 2 green
        {constant(0.0f), d65(), 15.0f},                                 // 3 light
    };
    Scene sc;
    sc.quads = {
        {{555, 0, 0}, {0, 555, 0}, {0, 0, 555}, 1},        // left (camera's left is +x)
        {{0, 0, 0}, {0, 555, 0}, {0, 0, 555}, 2},          // right
        {{0, 0, 0}, {555, 0, 0}, {0, 0, 555}, 0},          // floor
        {{0, 555, 0}, {555, 0, 0}, {0, 0, 555}, 0},        // ceiling
        {{0, 0, 555}, {555, 0, 0}, {0, 555, 0}, 0},        // back
        {{343, 554, 332}, {-130, 0, 0}, {0, 0, -105}, 3},  // light
    };
    auto rot_box = [&](Vec3 p, Vec3 size, float deg) {
        float r = deg * std::numbers::pi_v<float> / 180, c = std::cos(r), s = std::sin(r);
        add_box(sc.quads, p, Vec3{c, 0, -s} * size.x, {0, size.y, 0}, Vec3{s, 0, c} * size.z, 0);
    };
    rot_box({265, 0, 295}, {165, 330, 165}, 15);   // tall
    rot_box({130, 0, 65}, {165, 165, 165}, -18);   // short
    Camera cam({278, 278, -800}, {278, 278, 0}, {0, 1, 0}, 40, 1.0f);
    return render(sc, mats, constant(0.0f), cam, W, H, spp, 1.0f);
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
static Image prism(int spp, bool exaggerated) {
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
    Camera cam(X - d_out * 1.5f + sside * -2.5f + Y * 2.5f, look, Y, 55, float(W) / H);
    return render_hybrid(sc, mats, cam, W, H, 256, long(spp) * W * H, 1.0f);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: rt <swatch|normals|lights|cornell|metals|glass|prism|prism-x> <out.ppm> [spp]\n");
        return 2;
    }
    std::string mode = argv[1];
    Image img = mode == "swatch" ? swatch() : mode == "normals" ? normals()
                : mode == "lights"  ? lights()
                : mode == "cornell" ? cornell(argc > 3 ? std::atoi(argv[3]) : 1024)
                : mode == "metals"  ? metals(argc > 3 ? std::atoi(argv[3]) : 256)
                : mode == "glass"   ? glass(argc > 3 ? std::atoi(argv[3]) : 512)
                : mode == "prism"   ? prism(argc > 3 ? std::atoi(argv[3]) : 4096, false)
                : mode == "prism-x" ? prism(argc > 3 ? std::atoi(argv[3]) : 4096, true)
                                    : Image(0, 0);
    if (img.w == 0) {
        std::fprintf(stderr, "unknown mode %s\n", argv[1]);
        return 2;
    }
    if (!write_ppm(img, argv[2])) {
        std::fprintf(stderr, "could not write %s\n", argv[2]);
        return 1;
    }
    std::printf("wrote %s\n", argv[2]);
}
