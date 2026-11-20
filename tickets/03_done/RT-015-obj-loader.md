---
id: RT-015
title: OBJ mesh loading
milestone: M5
estimate: 3h
---
# RT-015 — OBJ mesh loading

**Why:** Real models for test scenes.

**Plan ref:** PLAN.md § Milestones → M5

**Touches:** src/obj.h

## Tasks
- [x] minimal OBJ parser (v, f, triangulate polygons)
- [x] per-mesh material

## Acceptance
- [x] Stanford bunny renders in glass

## Notes
- `src/obj.h`:
  - `load_obj(path, mat, out, err)` reads `v` and `f` lines and ignores everything else. It accepts `v/vt/vn`-style tokens and negative indices, and fan-triangulates polygons.
  - It rejects malformed vertices, out-of-range or zero indices and faces with fewer than 3 vertices, reporting `file:line`. On failure, `out` is left unchanged.
  - `fit_mesh()` scales and places a mesh.
- Tests: slash forms, negative indices, fan triangulation, each error path, and `fit_mesh` bounds.
- The bunny lives in `assets/bunny.obj` (gitignored; the download command is in CLAUDE.md). It's the Stanford 3D Scanning Repository model, for research use, and has 69,451 triangles. Its signed volume is +1.66, so the winding points outward, which the glass needs.
- `rt bunny <out> [spp]`: a BK7 glass bunny and a gold bunny, 139k triangles at 640×360 and 256 spp in 15.7 s.
- The glass looks busy for two reasons: the faces are flat-shaded, and each one disperses the light, so speckle builds up. **Follow-up:** smooth shading (interpolated `vn` or computed vertex normals) would make it look like real glass. The mesh also has the scan's open holes in its base.
- Each `load_obj` call gives the whole file one material; multiple materials per file (`usemtl`) aren't supported.
