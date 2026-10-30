# render-bench

Measures what this compositor actually costs per frame, on this machine, and saves each run so
two of them can be compared later.

```sh
tools/render-bench/render-bench.py run --label baseline --seconds 120
tools/render-bench/render-bench.py ab  --label dimming --seconds 30 --blocks 6 \
    --cmd-a "sed -i 's/^dim_alpha.*/dim_alpha = 0.85/' ~/.config/wayfire.ini" \
    --cmd-b "sed -i 's/^dim_alpha.*/dim_alpha = 1.0/'  ~/.config/wayfire.ini"
tools/render-bench/render-bench.py report tools/render-bench/runs/*
```

Needs `python-wayfire` and the `ipc` + `ipc-rules` plugins enabled. `intel-gpu-tools` is optional —
without it (or without passwordless sudo) you get compositor metrics only.

## What it samples

| Stream | Source | Kind |
|---|---|---|
| paint / guard / total budget | `wayfire/render-metrics` over IPC | **LEARNED** |
| frames over budget, effective Hz | derived from total budget vs refresh | derived |
| GPU engine busy, clocks, RC6, power | `intel_gpu_top -J` | **INSTANT** |
| CPU load | `/proc/loadavg` | **INSTANT** |

## Pitfalls — all three were hit for real on 2026-10-31

**1. `paint-budget` is LEARNED, not measured.** It is the adaptive scheduler's conservative
estimate: it ramps as load appears and decays slowly afterwards. Two *sequential* 30 s windows
produced a 57% difference from a change that cannot physically cost anything — the second window
had simply inherited a still-rising estimate. Symptom to watch for: the first window has a huge
spread (min 1.9, p95 17.4 ms) while the second is tight and high.
→ Compare LEARNED metrics only with `ab` mode, which alternates blocks so drift hits both
conditions equally. Or compare INSTANT metrics instead.

**2. `successful-presentations` is ROLLING, not cumulative.** Differencing it across samples gave
a *negative* frame rate. It is a windowed statistic and can decrease. There is no presentation
counter to derive fps from; use the effective-Hz estimate (below) or GPU busy %.

**3. GPU busy % alone hides the problem.** During genuine 96%-miss lag, Render/3D averaged 27%
and never exceeded 78%. A GPU that cannot finish a frame in time sits idle waiting for the vblank
it already missed, so moderate busy % plus high RC6 is *consistent with* missing every deadline.
Always read busy % next to the paint cost and the frame budget.

## Reading the numbers

- **frame budget** = `1 / refresh`. 165 Hz → 6.06 ms; 100 Hz → 10.0 ms.
- **frames over budget** — share of samples where paint + guard exceeds the budget. Those frames
  cannot present on their target vblank.
- **effective Hz** = `refresh / ceil(cost / frame)`. A missed frame waits a whole refresh interval,
  so the quantisation matters: an 11.5 ms cost gives ~82 Hz at 165 Hz but only ~50 Hz at 100 Hz.
  **This is why lowering the refresh rate made things feel worse** — see the baselines below.

## Baselines measured on this machine

3440×1440 Samsung LC34G55T over Thunderbolt→DP on the Intel TigerLake-H **GT1 (32 EU)** iGPU;
RTX 3060 asleep; mixed browser workload (Librewolf, Chromium, Discord).

| Date | State | Refresh | Paint median | Over budget | Effective |
|---|---|---|---|---|---|
| 2026-10-31 | software video decode | 165 Hz | 9.9 ms | 96% | ~82 Hz |
| 2026-10-31 | software video decode | 100 Hz | 10.0 ms | 68% | ~50 Hz ← felt worse |

Key observation from the same data: paint cost ranged **1.9 → 17.4 ms**. It is not a fixed cost —
quiet frames fit inside 6 ms comfortably. The expensive frames are client-driven (large damage
areas on a 4.95 Mpixel screen), which is where to look for wins.

## A fair comparison

1. Same workload both sides. Pick something repeatable (a specific page scrolling, a specific
   video) and write it in the label.
2. Warm up ~30 s before sampling; the estimator needs to reach the current load.
3. Use `ab` mode for anything config-related, with at least 4 blocks.
4. Check `load1` matched across conditions in the output. If it did not, the result is confounded
   and should be discarded, not explained.
5. Keep the run directories. `report` prints any of them side by side.
