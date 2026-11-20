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
- [ ] AABB per shape
- [ ] SAH or midpoint split BVH
- [ ] iterative traversal

## Acceptance
- [ ] same image as list-based (pixel diff ≈ 0)
- [ ] ≥10× faster on 10k-triangle scene
