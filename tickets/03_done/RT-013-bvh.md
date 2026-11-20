---
id: RT-013
title: BVH acceleration
milestone: M5
week: 8
estimate: 4h
---
# RT-013 — BVH acceleration

**Why:** Meshes are unusable without it.

**Plan ref:** PLAN.md § Architecture

**Touches:** src/bvh.h, src/geometry.h

## Tasks
- [x] AABB per shape
- [x] SAH or midpoint split BVH
- [x] iterative traversal

## Acceptance
- [x] same image as list-based (pixel diff ≈ 0)
- [x] ≥10× faster on 10k-triangle scene

## Notes
- `geometry.h`:
  - `AABB` uses a slab test whose comparisons are NaN-safe, so axis-parallel rays work.
  - `Scene::build()` builds a binned-SAH BVH (12 bins, split along the widest centroid axis, falling back to a median split). It holds spheres, quads and triangles together. Leaves hold up to 4 primitives, or stay larger only where splitting doesn't pay.
  - Traversal is iterative and visits the nearer child first.
- `render()` and `render_hybrid()` take the `Scene` by value and build the BVH themselves, so no scene setup can forget to. `intersect()` without a BVH does a linear scan; tests use that as the reference.
- Tests: 20,000 random rays over 301 random shapes, including a zero-thickness axis-aligned quad and axis-parallel rays. The BVH matches the linear scan exactly on hit/miss, t and material.
- `rt bvhbench`: a 10k-triangle sphere. Linear takes 1.61 s, the BVH 0.019 s (**87×**), with a max pixel difference of **0**.
- Existing scenes also got faster: glass 28 s → 10.9 s, Cornell (1024 spp) 49 s → 28.7 s.
- The ticket's `bvh.h` wasn't needed; everything lives in `geometry.h` next to `Scene`.
