#!/usr/bin/env python3
"""Mechanism figure: shows *why* the scale-sweep difficulty curve
(analysis/plot_sweep.py) exists. For each network size N, plots the
measured mesh diameter (public topology info, logged once per run by
scratch/livetrace-sim.cc into oracle_*.jsonl's topology_note -- read here
only for this explanatory figure, not fed to any online system) alongside
the mean true stepping-stone chain length (len(true_chain) - 1 per burst,
also oracle-only) that AttackerCampaign::ComputeChainLengthTarget derived
from that diameter.

The story in one picture: fixing average node degree (config topology.
avg_degree) instead of edge probability lets diameter grow ~log N with N;
chain length tracks diameter; and chain length growing against a fixed
live-window budget is what turns into the trace-success-rate falloff in
scale_sweep.png. See docs/summaries/phase6.md and docs/results.md.

Usage:
    python3 analysis/plot_mechanism.py --results-dir ../results \
        --n-values 20 40 80 160 320 640 --out ../docs/figures/mechanism_vs_n.png
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import japanize_matplotlib  # noqa: F401 -- registers a CJK-capable font for the Japanese labels below

from common import read_jsonl

DIAMETER_RE = re.compile(r"diameter=(\d+)")


def measurements_for_n(results_dir: Path, n: int):
    diameters = []
    chain_lengths = []
    for oracle_path in sorted(results_dir.glob(f"oracle_seed*_n{n}.jsonl")):
        records = read_jsonl(oracle_path)
        for rec in records:
            if rec.get("type") == "topology_note":
                m = DIAMETER_RE.search(rec.get("note", ""))
                if m:
                    diameters.append(int(m.group(1)))
            elif rec.get("type") == "burst":
                chain_lengths.append(len(rec["true_chain"]) - 1)
    return diameters, chain_lengths


def mean_and_range(values):
    if not values:
        return float("nan"), 0.0, 0.0
    mean = sum(values) / len(values)
    lo = mean - min(values)
    hi = max(values) - mean
    return mean, lo, hi


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-dir", type=Path, required=True)
    ap.add_argument("--n-values", type=int, nargs="+", required=True)
    ap.add_argument("--out", type=Path, required=True)
    cfg = ap.parse_args()

    ns, diam_mean, diam_lo, diam_hi = [], [], [], []
    chain_mean, chain_lo, chain_hi = [], [], []

    for n in cfg.n_values:
        diameters, chain_lengths = measurements_for_n(cfg.results_dir, n)
        if not diameters and not chain_lengths:
            continue
        ns.append(n)
        m, lo, hi = mean_and_range(diameters)
        diam_mean.append(m); diam_lo.append(lo); diam_hi.append(hi)
        m, lo, hi = mean_and_range(chain_lengths)
        chain_mean.append(m); chain_lo.append(lo); chain_hi.append(hi)

    if not ns:
        raise SystemExit(f"No oracle_seed*_n{{N}}.jsonl files found under {cfg.results_dir} "
                          f"for N in {cfg.n_values}")

    cfg.out.parent.mkdir(parents=True, exist_ok=True)

    fig, ax = plt.subplots(figsize=(7, 5))
    ax.errorbar(ns, diam_mean, yerr=[diam_lo, diam_hi], marker="o", capsize=4,
                linewidth=1.5, label="メッシュ直径(実測、seed間の範囲)")
    ax.errorbar(ns, chain_mean, yerr=[chain_lo, chain_hi], marker="s", capsize=4,
                linewidth=1.5, label="踏み台連鎖長(真値、seed間の範囲)")
    ax.set_xlabel("ネットワーク規模 N")
    ax.set_ylabel("ホップ数")
    ax.set_xscale("log")
    ax.set_title("N の増加が追跡難易度を駆動する仕組み\n(直径 ~log N で成長 → 連鎖長がそれに連動)")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(cfg.out, dpi=150)
    print(f"Wrote {cfg.out}")
    for n, dm, cm in zip(ns, diam_mean, chain_mean):
        print(f"  N={n}: diameter={dm:.2f}, chain_length={cm:.2f}")


if __name__ == "__main__":
    main()
