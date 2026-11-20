---
id: RT-005
title: Vec3, rays, sphere/plane/quad intersection
milestone: M2
week: 3
estimate: 3h
---
# RT-005 — Vec3, rays, sphere/plane/quad intersection

**Why:** Geometry to hit.

**Plan ref:** PLAN.md § Architecture

**Touches:** src/vec.h, src/geometry.h, tests/tests.cpp

## Tasks
- [x] Vec3 with the usual ops
- [x] Ray
- [x] Hit record (t, p, n, front_face, material id)
- [x] sphere, quad (Cornell walls) intersect
- [x] Scene = vector of shapes, closest-hit

## Acceptance
- [x] unit tests for hit/miss/inside-sphere cases

## Notes
- `src/vec.h`: `Vec3` and `Ray`. `src/geometry.h`: `Hit`, `Sphere`, `Quad` (a parallelogram given by a corner plus two edges u and v; its outward normal is u × v) and `Scene`, which keeps spheres and quads in separate vectors and returns the closest hit.
- Hit normals always face against the incoming ray, and `front` records which side was hit. RT-011's glass needs this.
- `ponytail:` marks the linear scan in `Scene`. RT-013 adds a BVH and will probably merge the shapes into a single list.
- Tests: sphere hit, miss, beyond tmax and from inside; quad hit, out of bounds and parallel; closest hit wins regardless of list order.
