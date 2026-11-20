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
- [x] dielectric with Fresnel + refraction
- [x] Cauchy and Sellmeier IOR (BK7, dense flint)
- [x] on dispersive bounce: terminate secondary λ, keep hero (×4 weight)

## Acceptance
- [x] glass sphere shows coloured fringes at edges
- [x] energy test: non-dispersive glass in furnace ≈ 1

## Notes
- `spectrum.h`: `cauchy(A, B)`, `sellmeier(...)`, plus `glass_bk7()` and `glass_sf11()` (Schott coefficients). Tests reproduce the catalogue n_d values 1.5168 and 1.78472.
- `material.h`: `Kind::Dielectric`, `dielectric(eta)` and `refract()`. Fresnel reuses `fresnel_conductor(cos, eta, 0)`, which handles total internal reflection without a special case.
- `trace()` chooses reflection or refraction with probability F. If the IOR differs across the 4 wavelengths, it calls `w.terminate_secondary()` and zeroes their throughput, so only the hero continues. Because `trace()` can modify the wavelength set, `w` is now passed by reference.
- Furnace: n = 1.5 glass gives 1.0000 and SF11 gives 1.0000. The furnace estimator is now pdf-weighted.
- `rt glass <out> [spp]`: BK7, SF11 and Cauchy(1.5, 0.06) over stripes, 640×300 at 512 spp in 28 s. The zoomed crop shows clear blue and orange fringes. There is chromatic speckle wherever a path loses its secondary wavelengths, which is the expected trade-off.
- Not included: the 1/η² radiance scaling (it cancels for closed objects viewed from air), absorption (Beer's law) and rough glass.
