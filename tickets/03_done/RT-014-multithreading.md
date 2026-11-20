---
id: RT-014
title: Multithreaded rendering
milestone: M5
estimate: 2h
---
# RT-014 — Multithreaded rendering

**Why:** Use all the cores.

**Plan ref:** PLAN.md § Key decisions → Parallelism

**Touches:** src/main.cpp

## Tasks
- [x] std::thread pool over rows (atomic row counter)
- [x] per-thread RNG

## Acceptance
- [x] speedup ≈ core count; image unchanged statistically

## Notes
- **Pulled forward** because the Cornell box at 1024 spp needed it.
- `render()` uses `std::thread`s pulling rows from an atomic counter. Each row seeds its own `Rng` from (seed, row), so the output is **bit-identical for any thread count** (tested with 1 thread vs 4).
- `lights`: 10.5 s → 1.67 s on 8 cores (6.3×).
