#!/usr/bin/python3
"""render-bench — measure this compositor's per-frame cost, honestly.

Samples three streams on one clock and writes a run directory you can diff later:

  wayfire/render-metrics (IPC)  the compositor's own scheduler view
  intel_gpu_top -J              per-engine GPU busy, clocks, RC6, power
  /proc/loadavg                 CPU contention

Why this exists: ad-hoc sampling produced two wrong conclusions in one session
(see README.md § Pitfalls). The metric kinds below are the fix — every number is
tagged INSTANT or LEARNED, and the A/B mode interleaves blocks because a LEARNED
metric cannot be compared across two sequential windows.

Usage
  render-bench.py run   --label idle --seconds 120
  render-bench.py ab    --label dimming --seconds 30 --blocks 6 \\
                        --cmd-a 'wf-set dim_alpha 0.85' --cmd-b 'wf-set dim_alpha 1.0'
  render-bench.py report <run-dir> [<run-dir> ...]      # summarise / compare

Needs: python-wayfire, intel-gpu-tools (for GPU sampling; optional), sudo for
intel_gpu_top. Everything else degrades gracefully.
"""

import argparse
import json
import math
import os
import shutil
import statistics as st
import subprocess
import sys
import time

# ---------------------------------------------------------------- metric kinds
#
# INSTANT  — a direct reading of the last interval. Safe to compare between short
#            windows, safe to average.
# LEARNED  — the adaptive scheduler's *estimate*, which ramps up under load and
#            decays slowly afterwards. NOT stationary: comparing two sequential
#            windows measures the estimator's memory as much as the change you
#            made. Only compare these across interleaved blocks, or runs minutes
#            apart under matched load.
# ROLLING  — a windowed counter maintained by the compositor. It can DECREASE, so
#            never derive a rate from successive differences.
#
METRIC_KIND = {
    "paint_ms":   "LEARNED",
    "guard_ms":   "LEARNED",
    "total_ms":   "LEARNED",
    "gpu_busy":   "INSTANT",
    "gpu_video":  "INSTANT",
    "gpu_freq":   "INSTANT",
    "gpu_rc6":    "INSTANT",
    "gpu_power":  "INSTANT",
    "load1":      "INSTANT",
    "presentations": "ROLLING",
}

SOCKET_DEFAULT = "/run/user/%d/wayfire-wayland-1-.socket" % os.getuid()
IGT_DEVICE = "pci:vendor=8086,device=9A60,card=0"   # the TigerLake iGPU


def wayfire_socket():
    return os.environ.get("WAYFIRE_SOCKET") or SOCKET_DEFAULT


def connect(output):
    os.environ["WAYFIRE_SOCKET"] = wayfire_socket()
    from wayfire import WayfireSocket
    sock = WayfireSocket()

    def metrics():
        r = sock.send_json({"method": "wayfire/render-metrics",
                            "data": {"output": output}})
        return r.get("result", r)

    return metrics


def start_gpu(path, seconds, period_ms=500):
    """intel_gpu_top writing a JSON stream; returns the Popen or None."""
    if not shutil.which("intel_gpu_top"):
        return None
    n = int(seconds * 1000 / period_ms) + 1
    cmd = ["sudo", "-n", "intel_gpu_top", "-J", "-s", str(period_ms),
           "-n", str(n), "-d", IGT_DEVICE, "-o", path]
    try:
        return subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
    except OSError:
        return None


def parse_gpu(path):
    """intel_gpu_top -J emits a sequence of top-level objects, not one array."""
    try:
        raw = open(path).read()
    except OSError:
        return []
    dec, out, i = json.JSONDecoder(), [], 0
    while i < len(raw):
        while i < len(raw) and raw[i] in " \t\r\n,[]":
            i += 1
        if i >= len(raw):
            break
        try:
            obj, i = dec.raw_decode(raw, i)
        except ValueError:
            break
        out.append(obj)
    return out[1:]  # first sample has no valid deltas


def gpu_rows(objs):
    rows = []
    for o in objs:
        try:
            rows.append({
                "gpu_busy":  o["engines"]["Render/3D"]["busy"],
                "gpu_video": o["engines"]["Video"]["busy"],
                "gpu_freq":  o["frequency"]["actual"],
                "gpu_rc6":   o["rc6"]["value"],
                "gpu_power": o["power"]["GPU"],
            })
        except (KeyError, TypeError):
            continue
    return rows


def sample_loop(metrics, out_path, seconds, block=None, period=0.5):
    """Sample the compositor + load every `period` s for `seconds`."""
    end = time.monotonic() + seconds
    with open(out_path, "a") as fh:
        while time.monotonic() < end:
            rec = {"t": round(time.monotonic(), 3)}
            if block is not None:
                rec["block"] = block
            try:
                m = metrics()
                c = m["composed"]
                rec.update(paint_ms=c["paint-budget-ns"] / 1e6,
                           guard_ms=c["miss-guard-ns"] / 1e6,
                           total_ms=c["total-budget-ns"] / 1e6,
                           refresh_ms=m["refresh-ns"] / 1e6,
                           path=m.get("predicted-path"),
                           scanouts=m.get("consecutive-scanouts"),
                           presentations=c.get("successful-presentations"))
            except Exception as exc:          # keep sampling through a hiccup
                rec["err"] = str(exc)
            try:
                rec["load1"] = float(open("/proc/loadavg").read().split()[0])
            except OSError:
                pass
            fh.write(json.dumps(rec) + "\n")
            fh.flush()
            time.sleep(period)


def describe(name, values):
    if not values:
        return None
    s = sorted(values)
    pick = lambda q: s[min(len(s) - 1, int(q * len(s)))]
    return {"metric": name, "kind": METRIC_KIND.get(name, "?"), "n": len(values),
            "median": st.median(values), "mean": st.mean(values),
            "p95": pick(0.95), "max": max(values), "min": min(values)}


def summarise(rows, gpu):
    """Everything a human needs, with the frame budget made explicit."""
    rows = [r for r in rows if "err" not in r]
    out = {"samples": len(rows), "gpu_samples": len(gpu)}
    if not rows:
        return out

    refresh = rows[0].get("refresh_ms")
    out["refresh_ms"] = refresh
    out["refresh_hz"] = 1000 / refresh if refresh else None

    stats = []
    for key in ("paint_ms", "total_ms", "load1"):
        d = describe(key, [r[key] for r in rows if key in r])
        if d:
            stats.append(d)
    for key in ("gpu_busy", "gpu_video", "gpu_freq", "gpu_rc6", "gpu_power"):
        d = describe(key, [g[key] for g in gpu if key in g])
        if d:
            stats.append(d)
    out["stats"] = stats

    if refresh:
        totals = [r["total_ms"] for r in rows if "total_ms" in r]
        over = [t for t in totals if t > refresh]
        out["over_budget_pct"] = 100 * len(over) / len(totals) if totals else None
        # If a frame needs k whole refresh intervals, it presents at refresh/k.
        eff = [1000 / (math.ceil(max(t, 1e-3) / refresh) * refresh) for t in totals]
        if eff:
            out["effective_hz_median"] = st.median(eff)
            out["effective_hz_worst"] = min(eff)
    scan = [r for r in rows if r.get("path") and r["path"] != "composed"]
    out["scanout_pct"] = 100 * len(scan) / len(rows)
    return out


def write_run(run_dir, meta, rows, gpu, summary):
    os.makedirs(run_dir, exist_ok=True)
    json.dump(meta, open(os.path.join(run_dir, "meta.json"), "w"), indent=1)
    json.dump(summary, open(os.path.join(run_dir, "summary.json"), "w"), indent=1)
    with open(os.path.join(run_dir, "report.txt"), "w") as fh:
        fh.write(render_report(meta, summary))
    return run_dir


def render_report(meta, s):
    L = []
    L.append(f"run        : {meta.get('label')}  ({meta.get('started')})")
    L.append(f"mode       : {meta.get('mode')}")
    if s.get("refresh_hz"):
        L.append(f"refresh    : {s['refresh_hz']:.1f} Hz  "
                 f"({s['refresh_ms']:.3f} ms frame budget)")
    L.append(f"samples    : {s.get('samples')} compositor / {s.get('gpu_samples')} gpu")
    L.append("")
    L.append(f"{'metric':12s} {'kind':8s} {'median':>8s} {'mean':>8s} {'p95':>8s} {'max':>8s}")
    for d in s.get("stats", []):
        L.append(f"{d['metric']:12s} {d['kind']:8s} {d['median']:8.2f} "
                 f"{d['mean']:8.2f} {d['p95']:8.2f} {d['max']:8.2f}")
    L.append("")
    if s.get("over_budget_pct") is not None:
        L.append(f"frames over budget : {s['over_budget_pct']:.0f}%")
    if s.get("effective_hz_median"):
        L.append(f"effective rate     : median {s['effective_hz_median']:.0f} Hz, "
                 f"worst {s['effective_hz_worst']:.0f} Hz")
    L.append(f"direct scanout     : {s.get('scanout_pct', 0):.0f}% of samples")
    L.append("")
    L.append("Reminder: LEARNED metrics ramp and decay; do not compare them across")
    L.append("two sequential windows. Use `ab` mode (interleaved) or GPU busy %.")
    return "\n".join(L) + "\n"


def cmd_run(args):
    metrics = connect(args.output)
    base = os.path.join(args.runs, time.strftime("%Y%m%d-%H%M%S") + "-" + args.label)
    os.makedirs(base, exist_ok=True)
    raw, gpu_path = os.path.join(base, "samples.jsonl"), os.path.join(base, "gpu.json")

    gpu_proc = start_gpu(gpu_path, args.seconds)
    if gpu_proc is None:
        print("note: intel_gpu_top unavailable (or sudo needs a password) — "
              "compositor metrics only", file=sys.stderr)

    print(f"sampling {args.seconds}s -> {base}", file=sys.stderr)
    sample_loop(metrics, raw, args.seconds)
    if gpu_proc:
        try:
            gpu_proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            gpu_proc.terminate()

    rows = [json.loads(l) for l in open(raw) if l.strip()]
    gpu = gpu_rows(parse_gpu(gpu_path))
    meta = {"label": args.label, "started": time.strftime("%F %T"),
            "seconds": args.seconds, "mode": "run", "output": args.output}
    summary = summarise(rows, gpu)
    write_run(base, meta, rows, gpu, summary)
    print(render_report(meta, summary))
    return base


def cmd_ab(args):
    """Interleaved A/B/A/B — the only valid way to compare a LEARNED metric."""
    metrics = connect(args.output)
    base = os.path.join(args.runs, time.strftime("%Y%m%d-%H%M%S") + "-ab-" + args.label)
    os.makedirs(base, exist_ok=True)
    raw = os.path.join(base, "samples.jsonl")

    for i in range(args.blocks):
        which = "A" if i % 2 == 0 else "B"
        cmd = args.cmd_a if which == "A" else args.cmd_b
        if cmd:
            subprocess.run(cmd, shell=True, check=False)
        time.sleep(args.settle)
        print(f"block {i+1}/{args.blocks} [{which}] {args.seconds}s", file=sys.stderr)
        sample_loop(metrics, raw, args.seconds, block=which)

    rows = [json.loads(l) for l in open(raw) if l.strip()]
    meta = {"label": args.label, "started": time.strftime("%F %T"), "mode": "ab",
            "blocks": args.blocks, "seconds": args.seconds,
            "cmd_a": args.cmd_a, "cmd_b": args.cmd_b, "output": args.output}
    out = {}
    for which in ("A", "B"):
        sub = [r for r in rows if r.get("block") == which and "err" not in r]
        out[which] = summarise(sub, [])
    json.dump(out, open(os.path.join(base, "summary.json"), "w"), indent=1)

    print(f"\ninterleaved A/B over {args.blocks} blocks of {args.seconds}s\n")
    for key in ("paint_ms", "total_ms", "load1"):
        a = next((d for d in out["A"].get("stats", []) if d["metric"] == key), None)
        b = next((d for d in out["B"].get("stats", []) if d["metric"] == key), None)
        if a and b:
            delta = b["median"] - a["median"]
            rel = 100 * delta / a["median"] if a["median"] else 0
            print(f"  {key:10s} A {a['median']:7.2f}   B {b['median']:7.2f}   "
                  f"delta {delta:+7.2f} ({rel:+.0f}%)")
    print("\nA blocks ran cmd-a, B blocks ran cmd-b; blocks alternate, so a drifting")
    print("estimator affects both conditions equally. Still check load1 matched.")
    return base


def cmd_report(args):
    for d in args.dirs:
        try:
            meta = json.load(open(os.path.join(d, "meta.json")))
            summary = json.load(open(os.path.join(d, "summary.json")))
        except OSError as exc:
            print(f"{d}: {exc}", file=sys.stderr)
            continue
        print(render_report(meta, summary))
        print("-" * 68)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--runs", default=os.path.join(here, "runs"),
                    help="directory for run output (default: ./runs)")
    ap.add_argument("--output", default="DP-3", help="Wayfire output name")
    sub = ap.add_subparsers(dest="cmd", required=True)

    r = sub.add_parser("run", help="single measurement window")
    r.add_argument("--label", required=True)
    r.add_argument("--seconds", type=int, default=120)
    r.set_defaults(func=cmd_run)

    a = sub.add_parser("ab", help="interleaved A/B comparison")
    a.add_argument("--label", required=True)
    a.add_argument("--seconds", type=int, default=30, help="per block")
    a.add_argument("--blocks", type=int, default=6, help="total blocks, alternating")
    a.add_argument("--settle", type=float, default=3.0,
                   help="seconds to wait after switching state before sampling")
    a.add_argument("--cmd-a", default="", help="shell command to enter state A")
    a.add_argument("--cmd-b", default="", help="shell command to enter state B")
    a.set_defaults(func=cmd_ab)

    p = sub.add_parser("report", help="print saved runs")
    p.add_argument("dirs", nargs="+")
    p.set_defaults(func=cmd_report)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
