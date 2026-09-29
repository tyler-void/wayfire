#!/usr/bin/env python3
"""Executable reference (independent oracle) for the 002 within-cluster layout.

The C++ `layout()` in plugins/spread-overview/src/layout.cpp MUST reproduce `layout_002()` below
within 0.5 px for the golden fixture (see design-brief.md §A). `layout_001()` is an exact port of
the 001 algorithm (equal slots + one uniform scale per cluster), kept as the baseline.

Usage:
  python3 layout_ref.py --golden golden-fixture.json   # emit golden rects for the fixture
  python3 layout_ref.py --png layout-compare.png       # render 001 vs 002 (boost 1.5 / 2.5)
  python3 layout_ref.py --fuzz 6000                    # overlap/bounds/non-positive + dense repros
  python3 layout_ref.py --fuzz-target 5000             # target geometry: smallest vs 001 (seed 5)
Only the standard library is needed, except --png (Pillow).
"""
import argparse
import json
import math
import random

# ------------------------------------------------------------------ geometry helpers

def cluster_region(ws, grid, out_w, out_h, cluster_gap, outer_margin):
    """Fixed-equal cluster region (invariant 9) — identical to 001, unchanged by 002."""
    ww, wh = out_w - 2 * outer_margin, out_h - 2 * outer_margin
    cw = max((ww - (grid[0] - 1) * cluster_gap) / grid[0], 1.0)
    ch = max((wh - (grid[1] - 1) * cluster_gap) / grid[1], 1.0)
    return (outer_margin + ws[0] * (cw + cluster_gap), outer_margin + ws[1] * (ch + cluster_gap), cw, ch)


# A member is a dict: {"id": int, "x": float, "y": float, "w": float, "h": float}
# x, y = top-left of the window in its OWN workspace's local coordinates (natural_pos).

def layout_001(region, members, spacing, max_scale):
    """Exact port of 001 pack_cluster(): equal slots, one uniform scale, order by id."""
    n = len(members)
    if n == 0:
        return {}
    rx, ry, rw, rh = region
    aspect = rw / max(1.0, rh)
    cols = max(1, min(int(round(math.sqrt(n * aspect))), n))
    rows = (n + cols - 1) // cols
    sw = max((rw - (cols + 1) * spacing) / cols, 1.0)
    sh = max((rh - (rows + 1) * spacing) / rows, 1.0)
    scale = max_scale
    for m in members:
        scale = min(scale, sw / max(1.0, m["w"]), sh / max(1.0, m["h"]))
    out = {}
    for i, m in enumerate(sorted(members, key=lambda m: m["id"])):
        r, c = divmod(i, cols)
        sx = rx + spacing + c * (sw + spacing)
        sy = ry + spacing + r * (sh + spacing)
        tw, th = m["w"] * scale, m["h"] * scale
        out[m["id"]] = (sx + (sw - tw) / 2, sy + (sh - th) / 2, tw, th)
    return out


def layout_002(region, members, spacing, max_scale, boost, out_h):
    """002 within-cluster layout: port of GNOME Shell UnalignedLayoutStrategy + _createBestLayout
    (js/ui/workspace.js, main @ 0062bde, 2026-09-27) with four documented deviations:
      D1  empty rows are dropped (GNOME can emit an empty row when one window is > 2x the ideal
          row width; it would still count toward row spacing).
      D2  a row's height only includes windows actually placed in it (GNOME updates
          row.fullHeight before deciding whether the window stays in the row).
      D3  the emphasis ratio h/out_h is clamped to [0, 1] (GNOME does not clamp, so a window
          taller than the output would get an emphasis below 1).
      D4  feasibility guard: a row count k whose spacing alone does not fit (or whose scale is
          <= 0) is skipped; k is bounded by n; if no k is feasible the cluster is laid out
          again with spacing 0. GNOME has no such guard and can yield zero/negative sizes.
    """
    n = len(members)
    if n == 0:
        return {}
    boost = max(1.0, boost)
    out = _layout_002_with_spacing(region, members, spacing, max_scale, boost, out_h)
    if out is None:                                         # D4 fallback
        out = _layout_002_with_spacing(region, members, 0.0, max_scale, boost, out_h)
    return out


def _layout_002_with_spacing(region, members, spacing, max_scale, boost, out_h):
    n = len(members)
    rx, ry, rw, rh = region
    ax, ay = rx + spacing, ry + spacing
    aw, ah = max(rw - 2 * spacing, 1.0), max(rh - 2 * spacing, 1.0)

    def b(m):  # per-window boost: lerp(boost, 1, clamp(h / out_h, 0, 1))   (D3: clamp)
        ratio = min(max(m["h"] / max(1.0, out_h), 0.0), 1.0)
        return boost + (1.0 - boost) * ratio

    def W(m):
        return max(1.0, m["w"])

    def H(m):
        return max(1.0, m["h"])

    def cy(m):
        return m["y"] + m["h"] / 2.0

    def cx(m):
        return m["x"] + m["w"] / 2.0

    def compute_layout(k):
        total = sum(W(m) * b(m) for m in members)
        ideal = total / k
        order = sorted(members, key=lambda m: (cy(m), m["id"]))
        rows, idx = [], 0
        for i in range(k):
            row = {"fw": 0.0, "fh": 0.0, "wins": []}
            rows.append(row)
            while idx < n:
                m = order[idx]
                w, h = W(m) * b(m), H(m) * b(m)
                keep = (row["fw"] + w <= ideal) or \
                    abs(1.0 - (row["fw"] + w) / ideal) < abs(1.0 - row["fw"] / ideal)
                if keep or i == k - 1:
                    row["wins"].append(m)
                    row["fw"] += w
                    row["fh"] = max(row["fh"], h)          # D2
                    idx += 1
                else:
                    break
        rows = [r for r in rows if r["wins"]]               # D1
        for r in rows:
            r["wins"].sort(key=lambda m: (cx(m), m["id"]))
        maxrow = rows[0]
        for r in rows[1:]:
            if r["fw"] > maxrow["fw"]:                      # strict: first max wins on ties
                maxrow = r
        return {"rows": rows, "maxcols": len(maxrow["wins"]), "gw": maxrow["fw"],
                "gh": sum(r["fh"] for r in rows)}

    def scale_space(L):
        hs = (L["maxcols"] - 1) * spacing
        vs = (len(L["rows"]) - 1) * spacing
        s = min((aw - hs) / L["gw"], (ah - vs) / L["gh"], max_scale)
        space = ((L["gw"] * s + hs) * (L["gh"] * s + vs)) / (aw * ah)
        return s, space

    def better(s0, sp0, s, sp):  # GNOME _isBetterScaleAndSpace, weights scale=1, space=0.1
        space_power, scale_power = (sp - sp0) * 0.1, (s - s0) * 1.0
        if s > s0 and sp > sp0:
            return True
        if s > s0 and sp <= sp0:
            return scale_power > space_power
        if s <= s0 and sp > sp0:
            return space_power > scale_power
        return False

    def feasible(L, s):  # D4
        if s <= 0.0 or (len(L["rows"]) - 1) * spacing >= ah:
            return False
        return all((len(r["wins"]) - 1) * spacing < aw for r in L["rows"])

    best, last_cols, s_best, sp_best = None, -1, 0.0, 0.0
    for k in range(1, n + 1):                               # D4: bounded by n
        cols = math.ceil(n / k)
        if cols == last_cols:
            break
        L = compute_layout(k)
        s, sp = scale_space(L)
        if not feasible(L, s):                              # D4: skip, keep searching
            continue
        if best is not None and not better(s_best, sp_best, s, sp):
            break
        best, last_cols, s_best, sp_best = L, cols, s, sp
    if best is None:
        return None                                         # caller retries with spacing 0

    rows, scale = best["rows"], s_best
    for r in rows:
        r["w"] = r["fw"] * scale + (len(r["wins"]) - 1) * spacing
        r["h"] = r["fh"] * scale
    hwo = sum(r["h"] for r in rows)
    vsp = (len(rows) - 1) * spacing
    add_v = min(1.0, (ah - vsp) / hwo)
    comp, y = 0.0, 0.0
    for r in rows:
        hsp = (len(r["wins"]) - 1) * spacing
        wwo = r["w"] - hsp
        add_h = min(1.0, (aw - hsp) / wwo)
        if add_h < add_v:
            r["add"] = add_h
            comp += (add_v - add_h) * r["h"]
        else:
            r["add"] = add_v
        r["x"] = ax + max(aw - (wwo * r["add"] + hsp), 0.0) / 2.0
        r["y"] = ay + max(ah - (hwo + vsp), 0.0) / 2.0 + y
        y += r["h"] * r["add"] + spacing
    comp /= 2.0

    out = {}
    for r in rows:
        row_y, row_h = r["y"] + comp, r["h"] * r["add"]
        x = r["x"]
        for m in r["wins"]:
            s = scale * b(m) * r["add"]
            cw, ch = W(m) * s, H(m) * s
            s_c = min(s, max_scale)
            tw, th = W(m) * s_c, H(m) * s_c
            tx = x + (cw - tw) / 2.0
            if len(rows) == 1:
                ty = row_y + (row_h - th) / 2.0
            else:
                ty = row_y + row_h - ch
            out[m["id"]] = (tx, ty, tw, th)
            x += cw + spacing
    return out


# ------------------------------------------------------------------ fixture (3440x1440, 3x3)

FIXTURE = {
    "output": [3440, 1440], "grid": [3, 3],
    "spacing": 20.0, "cluster_gap": 0.0, "outer_margin": 0.0, "max_scale": 1.0,
    # id, ws, x, y, w, h, label  (x, y workspace-local)
    "views": [
        [0, [0, 0], 0, 0, 3440, 1440, "librewolf"],
        [1, [0, 0], 2200, 600, 1100, 700, "kitty"],
        [2, [0, 0], 300, 800, 620, 460, "pavucontrol"],
        [3, [1, 0], 0, 0, 1720, 1440, "code"],
        [4, [1, 0], 1720, 0, 1720, 1440, "librewolf"],
        [5, [1, 0], 1500, 500, 400, 560, "calc"],
        [6, [2, 0], 0, 0, 1720, 1440, "kitty"],
        [7, [2, 0], 1900, 100, 1100, 700, "kitty"],
        [8, [2, 0], 2300, 850, 900, 560, "kitty"],
        [9, [2, 0], 1800, 500, 700, 900, "kitty"],
        [10, [0, 1], 0, 0, 3440, 1440, "librewolf"],
        [11, [1, 1], 600, 300, 1000, 700, "file dialog"],
        [12, [1, 1], 1900, 300, 800, 800, "imv"],
        [13, [0, 2], 200, 100, 1600, 1100, "obsidian"],
        [14, [0, 2], 1900, 150, 1300, 800, "kitty"],
        [15, [0, 2], 2000, 1000, 500, 350, "btop mini"],
        [16, [0, 2], 700, 1200, 420, 220, "notif"],
        [17, [2, 2], 1100, 400, 1200, 700, "mpv"],
    ],
}


def run_fixture(fx, algo, boost=1.5):
    ow, oh = fx["output"]
    res = {}
    for cyy in range(fx["grid"][1]):
        for cxx in range(fx["grid"][0]):
            ms = [{"id": v[0], "x": v[2], "y": v[3], "w": v[4], "h": v[5]}
                  for v in fx["views"] if tuple(v[1]) == (cxx, cyy)]
            if not ms:
                continue
            reg = cluster_region((cxx, cyy), fx["grid"], ow, oh, fx["cluster_gap"], fx["outer_margin"])
            if algo == "001":
                res.update(layout_001(reg, ms, fx["spacing"], fx["max_scale"]))
            else:
                res.update(layout_002(reg, ms, fx["spacing"], fx["max_scale"], boost, oh))
    return res


def violations(rects, region):
    ids, bad = list(rects), []
    rx, ry, rw, rh = region
    for i in ids:
        x, y, w, h = rects[i]
        if w <= 0 or h <= 0 or x < rx - 1e-6 or y < ry - 1e-6 or x + w > rx + rw + 1e-6 or y + h > ry + rh + 1e-6:
            bad.append(("bounds", i))
    for a in range(len(ids)):
        for c in range(a + 1, len(ids)):
            A, B = rects[ids[a]], rects[ids[c]]
            if A[0] < B[0] + B[2] - 1e-6 and B[0] < A[0] + A[2] - 1e-6 and \
               A[1] < B[1] + B[3] - 1e-6 and B[1] < A[1] + A[3] - 1e-6:
                bad.append(("overlap", ids[a], ids[c]))
    return bad


def dense_repros():
    """Degenerate cases found in review (GNOME's rule alone gives 0-size / negative / 1/0 here)."""
    reg = cluster_region((0, 0), (4, 2), 1920, 1080, 0.0, 0.0)          # 480x540 region
    big = {"id": 0, "x": 0, "y": 0, "w": 1920, "h": 1080}
    small = lambda i: {"id": i, "x": i * 10, "y": 500, "w": 60, "h": 60}
    return [
        (reg, [big] + [small(i) for i in range(1, 12)], 40.0, 1080),       # rows [1, 11] -> 0x0
        (reg, [big] + [small(i) for i in range(1, 13)], 40.0, 1080),       # -> negative widths
        (reg, [{"id": i, "x": 0, "y": 0, "w": 800, "h": 600} for i in range(85)], 40.0, 1080),  # s = 0
    ]


def fuzz(n_trials, seed=11, max_n=100):
    """Broad fuzz: varied grids/outputs/spacing, window sides 1 px..full output, 1..max_n windows
    (n <= 14 in half the trials so ordinary densities stay well sampled). Returns
    (violations, clusters where 002's smallest thumbnail < 001's, clusters checked)."""
    rng = random.Random(seed)
    worse_smallest, bad, checked = 0, 0, 0
    cases = [(reg, ms, sp, oh) for reg, ms, sp, oh in dense_repros()]
    for _ in range(n_trials):
        grid = rng.choice([(1, 1), (2, 2), (3, 3), (4, 2)])
        ow, oh = rng.choice([(3440, 1440), (1920, 1080), (2560, 1600)])
        gap = rng.choice([0.0, 40.0])
        sp = rng.choice([0.0, 8.0, 20.0, 40.0])
        reg = cluster_region((0, 0), grid, ow, oh, gap, 0.0)
        n = rng.randint(1, 14) if rng.random() < 0.5 else rng.randint(1, max_n)
        ms = []
        for i in range(n):
            w, h = rng.randint(1, ow), rng.randint(1, oh)
            ms.append({"id": i, "x": rng.randint(0, ow - w), "y": rng.randint(0, oh - h), "w": w, "h": h})
        cases.append((reg, ms, sp, oh))
    # per boost: [worse_all, eligible (001 smallest side >= 16 px), worse_eligible, worst_ratio_eligible]
    per_boost = {b: [0, 0, 0, float("inf")] for b in (1.0, 1.5, 2.5)}
    for reg, ms, sp, oh in cases:
        old = layout_001(reg, ms, sp, 1.0)
        mo = min(r[2] * r[3] for r in old.values())
        legible = min(min(r[2], r[3]) for r in old.values()) >= 16.0
        for boost in (1.0, 1.5, 2.5):
            new = layout_002(reg, ms, sp, 1.0, boost, oh)
            bad += len(violations(new, reg))
            checked += 1
            mn = min(r[2] * r[3] for r in new.values())
            st = per_boost[boost]
            if mn < mo - 1.0:
                worse_smallest += 1
                st[0] += 1
            if legible:
                st[1] += 1
                st[3] = min(st[3], mn / mo)
                if mn < mo - 1.0:
                    st[2] += 1
    return bad, worse_smallest, checked, per_boost


def fuzz_target(n_trials, seed=5):
    """Target geometry (3440x1440, 3x3, spacing 20, gap 0): clusters of 1..10 windows,
    200..3440 x 150..1440 px. Returns {boost: (worse_count, worst_ratio)} vs 001."""
    reg = cluster_region((0, 0), (3, 3), 3440, 1440, 0.0, 0.0)
    res = {}
    for boost in (1.0, 1.5, 2.5):
        rng = random.Random(seed)
        worse, worst = 0, float("inf")
        for _ in range(n_trials):
            n = rng.randint(1, 10)
            ms = []
            for i in range(n):
                w, h = rng.randint(200, 3440), rng.randint(150, 1440)
                ms.append({"id": i, "x": rng.randint(0, 3440 - w), "y": rng.randint(0, 1440 - h), "w": w, "h": h})
            old = layout_001(reg, ms, 20.0, 1.0)
            new = layout_002(reg, ms, 20.0, 1.0, boost, 1440)
            mo = min(r[2] * r[3] for r in old.values())
            mn = min(r[2] * r[3] for r in new.values())
            worst = min(worst, mn / mo)
            if mn < mo - 1.0:
                worse += 1
        res[boost] = (worse, worst)
    return res


def render_png(path):
    from PIL import Image, ImageDraw, ImageFont
    fx = FIXTURE
    ow, oh = fx["output"]
    variants = [("001 (current): equal slots, one uniform scale per workspace", run_fixture(fx, "001")),
                ("002 default: GNOME rows + small_window_boost 1.5", run_fixture(fx, "002", 1.5)),
                ("002 with small_window_boost 2.5", run_fixture(fx, "002", 2.5))]
    k, head = 0.5, 44
    pw, ph = int(ow * k), int(oh * k)
    img = Image.new("RGB", (pw, (ph + head) * len(variants)), (17, 17, 27))
    d = ImageDraw.Draw(img)
    try:
        fT = ImageFont.truetype("DejaVuSans-Bold.ttf", 24)
        fL = ImageFont.truetype("DejaVuSans.ttf", 13)
        fS = ImageFont.truetype("DejaVuSansMono.ttf", 11)
    except OSError:
        fT = fL = fS = ImageFont.load_default()
    for p, (title, res) in enumerate(variants):
        oy = p * (ph + head)
        d.text((12, oy + 9), title, font=fT, fill=(205, 214, 244))
        oy += head
        d.rectangle([0, oy, pw - 1, oy + ph - 1], fill=(30, 30, 46))
        for cyy in range(fx["grid"][1]):
            for cxx in range(fx["grid"][0]):
                rx, ry, rw, rh = cluster_region((cxx, cyy), fx["grid"], ow, oh, fx["cluster_gap"], fx["outer_margin"])
                d.rectangle([rx * k, oy + ry * k, (rx + rw) * k, oy + (ry + rh) * k], outline=(88, 91, 112))
        for v in fx["views"]:
            x, y, w, h = res[v[0]]
            box = [x * k, oy + y * k, (x + w) * k, oy + (y + h) * k]
            d.rectangle(box, fill=(70, 80, 120), outline=(137, 180, 250), width=2)
            if w * k > 38 and h * k > 16:
                d.text((box[0] + 4, box[1] + 2), v[6], font=fL, fill=(255, 255, 255))
                if h * k > 30:
                    d.text((box[0] + 4, box[1] + 17), f"{int(w)}x{int(h)}", font=fS, fill=(230, 230, 240))
            else:
                d.text((box[2] + 3, box[1]), f"{v[6]} {int(w)}x{int(h)}", font=fS, fill=(137, 180, 250))
    img.save(path)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--golden")
    ap.add_argument("--png")
    ap.add_argument("--fuzz", type=int)
    ap.add_argument("--fuzz-target", type=int)
    a = ap.parse_args()
    if a.golden:
        g = {"fixture": FIXTURE, "tolerance_px": 0.5, "expected": {}}
        g["expected"]["001"] = {str(i): r for i, r in run_fixture(FIXTURE, "001").items()}
        for boost in (1.0, 1.5, 2.5):
            g["expected"][f"002_boost_{boost}"] = {str(i): r for i, r in run_fixture(FIXTURE, "002", boost).items()}
        with open(a.golden, "w") as f:
            json.dump(g, f, indent=1)
        print("wrote", a.golden)
    if a.png:
        render_png(a.png)
        print("wrote", a.png)
    if a.fuzz:
        bad, worse, checked, per_boost = fuzz(a.fuzz)
        print(f"fuzz: {checked} layouts ({a.fuzz} random clusters + 3 dense repros, x 3 boosts): "
              f"{bad} overlap/bounds/non-positive violations; smallest thumbnail worse than 001 in {worse}")
        for boost, (w_all, elig, w_elig, worst) in per_boost.items():
            print(f"  boost {boost}: worse in {w_all} (any density); where 001's smallest side >= 16 px "
                  f"({elig} clusters): worse in {w_elig}, worst ratio {worst:.2f}")
    if a.fuzz_target:
        for boost, (worse, worst) in fuzz_target(a.fuzz_target).items():
            print(f"target 3440x1440 3x3 sp20, {a.fuzz_target} clusters, boost {boost}: "
                  f"smallest worse than 001 in {worse}, worst ratio {worst:.2f}")
