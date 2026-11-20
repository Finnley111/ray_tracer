---
id: RT-016
title: Next-event estimation + MIS
milestone: M6
week: 10
estimate: 5h
---
# RT-016 — Next-event estimation + MIS

**Why:** Small lights (and the prism slit) converge far faster.

**Plan ref:** PLAN.md § Key decisions → Sampling

**Touches:** src/integrator.h, src/geometry.h

## Tasks
- [ ] light sampling on quads/spheres
- [ ] power heuristic MIS with BSDF sampling

## Acceptance
- [ ] Cornell box at 64 spp NEE has less noise than 1024 spp without
- [ ] images agree in mean
