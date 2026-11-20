---
id: RT-011
title: Glass with wavelength-dependent IOR
milestone: M4
week: 7
estimate: 4h
---
# RT-011 — Glass with wavelength-dependent IOR

**Why:** Dispersion is the signature spectral effect.

**Plan ref:** PLAN.md § Key decisions → Dispersion

**Touches:** src/material.h, src/integrator.h

## Tasks
- [ ] dielectric with Fresnel + refraction
- [ ] Cauchy and Sellmeier IOR (BK7, dense flint)
- [ ] on dispersive bounce: terminate secondary λ, keep hero (×4 weight)

## Acceptance
- [ ] glass sphere shows coloured fringes at edges
- [ ] energy test: non-dispersive glass in furnace ≈ 1
