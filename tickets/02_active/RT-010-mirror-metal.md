---
id: RT-010
title: Specular mirror + spectral conductor (Fresnel)
milestone: M4
week: 7
estimate: 2h
---
# RT-010 — Specular mirror + spectral conductor (Fresnel)

**Why:** Gold/copper colour comes from spectral IOR.

**Plan ref:** PLAN.md § Key decisions → Spectra

**Touches:** src/material.h

## Tasks
- [x] perfect mirror
- [x] conductor with sampled complex IOR (η, k) for Au, Cu, Ag

## Acceptance
- [x] gold sphere looks gold under D65 without any RGB tinting

## Notes
- `Material` now has a `kind` field: `Diffuse`, `Mirror` or `Conductor`. Helpers `mirror(r)` and `conductor(eta, k)` build the latter two. `fresnel_conductor()` uses the exact complex-IOR Fresnel formula and reduces to the dielectric case when k = 0.
- `src/metals.h`: Johnson & Christy 1972 n,k for Au, Ag and Cu, 342–892 nm, generated from the raw refractiveindex.info YAML (CC0). I downloaded the files myself and checked them against the agent's copy; they match exactly.
- Normal-incidence colour under D65 (linear sRGB): Au (1.02, 0.72, 0.36), Cu (0.92, 0.62, 0.52), Ag (0.98, 0.97, 0.97). Tests assert gold is yellow, copper is orange-red and silver is neutral.
- `rt metals <out> [spp]`: 640×300 at 256 spp in 10 s. Metals are perfectly smooth for now. Add rough metals (GGX) if a scene needs them.
