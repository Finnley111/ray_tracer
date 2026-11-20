---
id: RT-012
title: Prism rainbow scene
milestone: M4
week: 8
estimate: 3h
---
# RT-012 — Prism rainbow scene

**Why:** The showcase effect.

**Plan ref:** PLAN.md § Goal

**Touches:** src/geometry.h (triangles), src/main.cpp

## Tasks
- [x] triangle intersection
- [x] triangular prism mesh
- [x] narrow slit light behind prism, diffuse screen

## Acceptance
- [x] renders/prism.ppm shows a separated spectrum on the screen

## Notes
- `geometry.h`: `Triangle` (Möller–Trumbore; the outward normal follows the winding) and `orient_convex()` to fix the winding on convex meshes. `Scene` gains a `tris` list.
- Scene setup: the beam path at 550 nm and minimum deviation is computed in code, and the slit wall, light strip and screen are placed along it. Results: SF11 deviates 67.2° with a 2.8° spread from 450 to 650 nm; Cauchy(1.5, 0.05) deviates 52.7° with a 6.85° spread.
- **Brute-force path tracing can't render this.** A screen point only lights up if a random bounce happens to go back through the prism and slit and hit the light at a matching wavelength, which is about 1 in 10⁴ per sample.
- **Added a light tracer** (`light_path`, `render_hybrid` in `integrator.h`):
  - It fires paths from area lights and connects each diffuse vertex to the pinhole camera (`Camera::project`, with We = 1/(A·cos⁴θ)).
  - Camera paths handle everything whose first hit isn't diffuse, i.e. lights seen directly and anything seen through glass or mirrors. The light tracer handles the rest. The two sets don't overlap, so nothing is counted twice.
  - Check: the hybrid and the pure path tracer agree to within 0.5% on a diffuse test scene (tested at < 3%).
  - Limits: only area-light quads are supported (no environment light), and `scatter()` and `roulette()` are now shared with `trace()`.
- `rt prism <out> [light paths per pixel]` uses real SF11 and takes 66 s at 4096. It shows a sharp, curved spectrum; the curve comes from rays reaching the prism at steeper vertical angles.
- `rt prism-x` uses the exaggerated glass and takes 81 s. Its band is wider and it also throws a faint rainbow onto the floor.
- Known issues:
  - The prism itself is speckly because camera paths through glass rely on brute force.
  - The slit wall casts a shadow from the fill light onto part of the screen. This is physically correct.
  - Full bidirectional path tracing (BDPT) with MIS would fix the speckle; that's a candidate stretch ticket.
