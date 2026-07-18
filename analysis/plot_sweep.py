#!/usr/bin/env python3
"""Phase 5/6 (quantitative half): turns results/sweep_summary.csv (written by
evaluate_phase4.py) into the scale-sweep figures -- the project's headline
result. Rate-metric panels (trace success rate, re-identification recall)
use asymmetric Wilson 95% CI error bars; continuous-metric panels (hops
reached, time-to-trace) use symmetric t-distribution 95% CI error bars. Each
point is additionally annotated with its trial/sample count so a reader
can't mistake a small-n cell for a precise one.
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def load_csv(path: Path):
    with path.open() as f:
        return list(csv.DictReader(f))


def as_float(row, key):
    v = row.get(key, "")
    try:
        f = float(v)
        return f
    except (ValueError, TypeError):
        return float("nan")


def plot_rate_panel(ax, rows, ns, p_key, lo_key, hi_key, trials_key, title):
    means = [as_float(r, p_key) for r in rows]
    los = [as_float(r, lo_key) for r in rows]
    his = [as_float(r, hi_key) for r in rows]
    trials = [as_float(r, trials_key) for r in rows]
    # max(0.0, ...) guards against floating-point noise at the boundary
    # (e.g. a Wilson upper bound landing at 0.9999999999999999 against a
    # mean of exactly 1.0), which matplotlib's errorbar rejects outright if
    # a computed yerr comes out fractionally negative.
    yerr_lo = [max(0.0, m - lo) if m == m and lo == lo else 0.0 for m, lo in zip(means, los)]
    yerr_hi = [max(0.0, hi - m) if m == m and hi == hi else 0.0 for m, hi in zip(means, his)]
    ax.errorbar(ns, means, yerr=[yerr_lo, yerr_hi], marker="o", capsize=4, linewidth=1.5)
    for x, y, k in zip(ns, means, trials):
        if y == y:  # not NaN
            label = f"n={int(k)}" if k == k else "n=0"
            ax.annotate(label, (x, y), textcoords="offset points", xytext=(0, 8), fontsize=7, ha="center")
    ax.set_xlabel("Network size N")
    ax.set_title(title, fontsize=10)
    ax.set_xscale("log")
    ax.set_ylim(-0.05, 1.05)
    ax.grid(True, alpha=0.3)


def plot_continuous_panel(ax, rows, ns, mean_key, ci_key, n_key, title):
    means = [as_float(r, mean_key) for r in rows]
    cis = [as_float(r, ci_key) for r in rows]
    ns_samples = [as_float(r, n_key) for r in rows]
    # matplotlib's errorbar chokes on NaN in yerr; substitute 0 and let the
    # point itself (also NaN-safe via masking) signal "no CI computed".
    yerr = [0.0 if c != c else c for c in cis]
    ax.errorbar(ns, means, yerr=yerr, marker="o", capsize=4, linewidth=1.5)
    for x, y, k in zip(ns, means, ns_samples):
        if y == y:
            label = f"n={int(k)}" if k == k else "n=0"
            ax.annotate(label, (x, y), textcoords="offset points", xytext=(0, 8), fontsize=7, ha="center")
    ax.set_xlabel("Network size N")
    ax.set_title(title, fontsize=10)
    ax.set_xscale("log")
    ax.grid(True, alpha=0.3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", type=Path, required=True)
    ap.add_argument("--out-dir", type=Path, required=True)
    cfg = ap.parse_args()

    rows = load_csv(cfg.csv)
    rows.sort(key=lambda r: int(r["n"]))
    ns = [int(r["n"]) for r in rows]

    cfg.out_dir.mkdir(parents=True, exist_ok=True)

    fig, axes = plt.subplots(2, 2, figsize=(12, 9))
    plot_rate_panel(axes[0][0], rows, ns, "trace_success_rate_p", "trace_success_rate_lo",
                     "trace_success_rate_hi", "trace_success_rate_trials",
                     "Trace success rate\n(reached true origin within 5s, Wilson 95% CI)")
    plot_continuous_panel(axes[0][1], rows, ns, "hops_reached_mean", "hops_reached_ci", "hops_reached_n",
                           "Hops reached within live window\n(t-dist 95% CI)")
    plot_continuous_panel(axes[1][0], rows, ns, "time_to_trace_mean", "time_to_trace_ci", "time_to_trace_n",
                           "Time-to-trace, successful only (s)\n(t-dist 95% CI)")
    plot_rate_panel(axes[1][1], rows, ns, "reid_recall_p", "reid_recall_lo", "reid_recall_hi",
                     "reid_recall_trials", "Re-identification pairwise recall\n(Wilson 95% CI)")

    fig.suptitle("LiveTrace scale sweep: pooled Wilson CI (rates) / t-dist CI (continuous)", fontsize=12)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    out_path = cfg.out_dir / "scale_sweep.png"
    fig.savefig(out_path, dpi=150)
    print(f"Wrote {out_path}")


if __name__ == "__main__":
    main()
