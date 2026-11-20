---
id: RT-006
title: Pinhole camera + normals render
milestone: M2
estimate: 2h
---
# RT-006 — Pinhole camera + normals render

**Why:** See the geometry before shading it.

**Plan ref:** PLAN.md § Architecture

**Touches:** src/camera.h, src/main.cpp

## Tasks
- [x] look-from/look-at/vfov camera
- [x] jittered pixel sampling
- [x] debug mode: shade by normal

## Acceptance
- [x] renders/normals.ppm shows spheres + floor correctly oriented

## Notes
- `src/camera.h`: pinhole camera taking from, at, up, vertical FOV and aspect. `ray(u, v)` has its origin at the top-left. 4 jittered samples per pixel.
- `rt` now takes a mode: `rt swatch <out>` or `rt normals <out>`. The normals view checked out: floor +y is green, +x is pink, bottoms are purple, edges are antialiased.
- Tests: the centre ray hits the look-at point, and the corner ray matches a 90° vertical FOV.
