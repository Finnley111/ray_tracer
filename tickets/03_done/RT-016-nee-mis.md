---
id: RT-016
title: Next-event estimation + MIS
milestone: M6
estimate: 5h
---
# RT-016 — Next-event estimation + MIS

**Why:** Small lights (and the prism slit) converge far faster.

**Plan ref:** PLAN.md § Key decisions → Sampling

**Touches:** src/integrator.h, src/geometry.h

## Tasks
- [x] light sampling on quads/spheres
- [x] power heuristic MIS with BSDF sampling

## Acceptance
- [x] Cornell box at 64 spp NEE has less noise than 1024 spp without
- [x] images agree in mean

## Notes
- `Scene::lights` holds every emissive sphere and quad and is filled by `render()`. `Scene::sample_light()` picks a uniform point on a light (sphere: uniform over its whole area, which works from inside too). `Hit` now records `shape` and `area` so the MIS weights can be computed when a light is hit.
- `trace()` changes:
  - At each diffuse vertex it samples one light, chosen uniformly, and casts a shadow ray (next-event estimation, NEE).
  - When a BSDF-sampled ray hits a light, that light's contribution is weighted with the power heuristic against the NEE pdf. Emissive triangles aren't sampled, so they keep full BSDF weight.
  - `scene.use_nee = false` gives plain BSDF sampling.
- **Changed: lights are now one-sided** (quad: along u × v; sphere: outward), with `Material::two_sided` as an opt-in. The two-sided shortcut from RT-008 was not harmless. The Cornell light's back face lit the 1-unit gap above it, and NEE turned that into fireflies. All scene lights already face the right way. One-sided emission is applied to NEE, MIS and the light tracer.
- Tests:
  - Inside an emitting sphere with NEE gives 1.0027 (expected 1).
  - NEE on and off agree in the mean to within 1.3%.
  - A one-sided panel emits on its front side only.
  - PT and hybrid still agree.
- `rt neebench` (Cornell box; noise = RMS between two seeds on display values clamped to [0, 1]):
  - NEE+MIS at 64 spp: 4.0 s, noise 0.0119.
  - BSDF-only at 1024 spp: 33.6 s, noise 0.0230.
  - So NEE has **1.93× lower noise with 1/16 the samples**, and the means agree to 0.16%.
  - The metric is clamped because unclamped RMS is dominated by anti-aliasing at the light's edges, which is 100× brighter than the room and identical for both methods.
- Visual check: `lights` at 128 spp is now nearly clean, and the prism still works.
