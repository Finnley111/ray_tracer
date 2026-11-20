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
- [ ] perfect mirror
- [ ] conductor with sampled complex IOR (η, k) for Au, Cu, Ag

## Acceptance
- [ ] gold sphere looks gold under D65 without any RGB tinting
