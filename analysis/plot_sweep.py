#!/usr/bin/env python3
"""Phase 5 (quantitative half): turns results/sweep_summary.csv (written by
evaluate_phase4.py) into the scale-sweep figures -- the project's headline
result. Each panel is mean +/- 95% CI across seeds, as a function of N.
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
        return float(v)
    except ValueError:
        return float("nan")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", type=Path, required=True)
    ap.add_argument("--out-dir", type=Path, required=True)
    cfg = ap.parse_args()

    rows = load_csv(cfg.csv)
    rows.sort(key=lambda r: int(r["n"]))
    ns = [int(r["n"]) for r in rows]

    panels = [
        ("trace_success_rate", "Trace success rate\n(reached true origin within 5s)", (0, 1.05)),
        ("hops_reached", "Hops reached within live window", None),
        ("time_to_trace", "Time-to-trace, successful only (s)", None),
        ("reid_recall", "Re-identification pairwise recall", (0, 1.05)),
    ]

    cfg.out_dir.mkdir(parents=True, exist_ok=True)

    fig, axes = plt.subplots(2, 2, figsize=(11, 8))
    for ax, (key, title, ylim) in zip(axes.flat, panels):
        means = [as_float(r, f"{key}_mean") for r in rows]
        cis = [as_float(r, f"{key}_ci") for r in rows]
        ax.errorbar(ns, means, yerr=cis, marker="o", capsize=4, linewidth=1.5)
        ax.set_xlabel("Network size N")
        ax.set_title(title, fontsize=10)
        ax.set_xscale("log")
        if ylim:
            ax.set_ylim(*ylim)
        ax.grid(True, alpha=0.3)

    fig.suptitle("LiveTrace Phase 4 scale sweep: mean ± 95% CI across seeds", fontsize=12)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    out_path = cfg.out_dir / "scale_sweep.png"
    fig.savefig(out_path, dpi=150)
    print(f"Wrote {out_path}")


if __name__ == "__main__":
    main()
