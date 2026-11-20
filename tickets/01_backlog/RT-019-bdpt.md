---
id: RT-019
title: Bidirectional path tracing (BDPT) with MIS
milestone: M7
week: stretch (Feb 2027)
estimate: 12-16h
---
# RT-019 — Bidirectional path tracing (BDPT) with MIS

**Why:** Two kinds of noise are left in the showcase: chromatic speckle when looking *through* the prism, and specks where the gold sphere reflects caustics on the floor. Both are specular → diffuse → specular paths. The camera tracer (`trace`) and the light tracer (`light_path`) each sample them badly, and `render_hybrid` can only pick one of the two per path. BDPT (Veach 1997, ch. 10) builds a camera subpath and a light subpath, connects every pair of vertices, and weights all the strategies with MIS. It replaces `render_hybrid`.

**Plan ref:** PLAN.md § Key decisions → Sampling, Caustics

**Touches:** src/integrator.h (new `bdpt.h` only if it grows past ~250 lines), src/camera.h (pdf of a camera direction), src/geometry.h (light pdfs, reusing `sample_light`), src/main.cpp, src/scene_file.h (`renderer bdpt`), tests/tests.cpp

## Design notes
- **Vertex record:** position, normal, material, `beta`, `pdf_fwd`, `pdf_rev` (area measure), a `delta` flag (mirror, conductor or dielectric), and the vertex type (camera, light, surface).
- **Wavelengths:** sample one `SampledWavelengths` per pixel sample and share it between both subpaths. If either subpath crosses dispersive glass, the joined path keeps only the hero wavelength. Terminate secondaries on a *copy* for each connection, so one connection doesn't strip wavelengths from another. This must match how `to_xyz` handles the pdf.
- **Strategies (s light vertices, t camera vertices):**
  - s = 0: the camera path hits an emitter.
  - s = 1: NEE. The existing `sample_light` logic gets a full pdf_rev.
  - t = 1: splat to the film. The existing `light_path` camera connection becomes the general case.
  - s, t ≥ 2: connect through a shadow ray with a geometry term.
  - Connections are skipped when either endpoint is `delta`.
- **MIS:** start with the balance heuristic computed from the pdf_fwd/pdf_rev ratio chain (pbrt-v4 `MISWeight` pattern), with delta vertices treated as pdf 1 and excluded from the sum. Switch to the power heuristic only if it measurably helps.
- **Lights:** one-sided by default and `two_sided` aware, matching RT-016. Area lights only (quads and spheres); the environment/background stays s = 0 only, so document that.
- **Film:** splats (t = 1) go to per-thread XYZ buffers, as in `render_hybrid`. Everything else accumulates per pixel. Combine with the same `W*H / n_paths` normalisation, adjusted for camera paths per pixel.
- **Keep** `trace()` and `render()` as the fast default for scenes without caustics. BDPT is opt-in: `renderer bdpt` in scene files, `showcase-bdpt` mode.

## Tasks
- [ ] Camera: `pdf_we(ray)`, i.e. the solid-angle pdf of a camera ray (pinhole: 1 / (film_area · cos³θ))
- [ ] Light: area pdf and emission pdf for a point and direction on a quad or sphere light
- [ ] `random_walk()` shared by both subpaths, recording vertices with pdf_fwd and filling the previous vertex's pdf_rev
- [ ] `connect(s, t)` for every strategy, including the t = 1 splat and s = 1 light resampling
- [ ] MIS weight via pdf ratios; leave a `ponytail:` note on any approximation
- [ ] `render_bdpt()` with threads, splat buffers and a deterministic seed per row/chunk
- [ ] Scene-file `renderer bdpt`; `rt showcase-bdpt`
- [ ] Tests (below)

## Acceptance
- [ ] **Unbiased:** on the RT-016 test scene (quad and sphere light, occluder) and the furnace-inside-a-sphere scene, BDPT's mean matches `render()` within 2%. On the prism scene it matches `render_hybrid` within 3% on the screen region.
- [ ] **MIS sanity:** for one fixed path of length 3 in a diffuse-only scene, the weights over all (s, t) strategies sum to 1 (±1e-4).
- [ ] **Better where it matters:** on `showcase` at equal wall-clock time, BDPT has lower two-seed RMS noise (as in `neebench`, display-clamped) than `render_hybrid` over (a) the prism's screen-space bounding box and (b) the gold sphere's. Report both numbers. It must not be worse overall.
- [ ] `renders/showcase_bdpt.png` reviewed side by side with `showcase.png`.
- [ ] `rt_tests` passes; no warnings.

## References
- E. Veach, *Robust Monte Carlo Methods for Light Transport Simulation*, PhD thesis 1997, ch. 10.
- Pharr, Jakob, Humphreys, *Physically Based Rendering* 4th ed., §13.3 (BDPT; MIS weight via pdf ratios).
