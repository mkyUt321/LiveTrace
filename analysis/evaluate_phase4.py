#!/usr/bin/env python3
"""Phase 4: the scale-sweep main result.

Aggregates the same per-run metrics as evaluate_phase1/2/3.py, but as a
function of network size N: mean +/- 95% CI across seeds for each N. Writes
a CSV (results/sweep_summary.csv by default) that analysis/plot_sweep.py
(Phase 5) turns into figures. A cell where the traceback never reaches the
origin at some N is not an error -- it's recorded as a 0.0 success rate,
exactly the kind of negative observation the project treats as a real
result.

Usage:
    python3 analysis/evaluate_phase4.py --results-dir ../results \
        --n-values 20 40 80 160 320 --seeds 1 2 3 4 5
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path
from types import SimpleNamespace

from common import load_run, mean_ci95
from evaluate_phase1 import evaluate_run as evaluate_phase1_run
from evaluate_phase2 import evaluate_run as evaluate_phase2_run
from evaluate_phase3 import evaluate_run as evaluate_phase3_run


def summarize_n(results_dir: Path, n: int, seeds: list[int], p1_cfg) -> dict:
    per_seed_recall1, all_gaps = [], []
    per_seed_success, per_seed_hops, all_ttt = [], [], []
    per_seed_precision3, per_seed_recall3 = [], []
    bursts_total = 0

    for seed in seeds:
        run = load_run(results_dir, seed, n)
        bursts_total += len(run.oracle_bursts)

        hits, _prec, gaps = evaluate_phase1_run(run, p1_cfg)
        if hits:
            per_seed_recall1.append(sum(hits) / len(hits))
        all_gaps += gaps

        succ, hops, ttt = evaluate_phase2_run(run)
        if succ:
            per_seed_success.append(sum(succ) / len(succ))
        if hops:
            per_seed_hops.append(sum(hops) / len(hops))
        all_ttt += ttt

        p3, r3, _ = evaluate_phase3_run(run)
        if p3 == p3:
            per_seed_precision3.append(p3)
        if r3 == r3:
            per_seed_recall3.append(r3)

    def agg(values):
        m, ci, k = mean_ci95(values)
        return m, ci, k

    row = {"n": n, "bursts_total": bursts_total}
    row["single_hop_recall_mean"], row["single_hop_recall_ci"], _ = agg(per_seed_recall1)
    row["score_gap_mean"], row["score_gap_ci"], _ = agg(all_gaps)
    row["trace_success_rate_mean"], row["trace_success_rate_ci"], _ = agg(per_seed_success)
    row["hops_reached_mean"], row["hops_reached_ci"], _ = agg(per_seed_hops)
    row["time_to_trace_mean"], row["time_to_trace_ci"], _ = agg(all_ttt)
    row["reid_precision_mean"], row["reid_precision_ci"], _ = agg(per_seed_precision3)
    row["reid_recall_mean"], row["reid_recall_ci"], _ = agg(per_seed_recall3)
    return row


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-dir", type=Path, required=True)
    ap.add_argument("--n-values", type=int, nargs="+", required=True)
    ap.add_argument("--seeds", type=int, nargs="+", required=True)
    ap.add_argument("--live-window", type=float, default=5.0)
    ap.add_argument("--accumulation", type=float, default=2.0)
    ap.add_argument("--bucket-s", type=float, default=0.05)
    ap.add_argument("--on-threshold-pps", dest="on_pps", type=float, default=1.0)
    ap.add_argument("--min-transitions", type=int, default=3)
    ap.add_argument("--max-lag-buckets", dest="max_lag", type=int, default=3)
    ap.add_argument("--score-threshold", type=float, default=0.5)
    ap.add_argument("--out-csv", type=Path, default=None)
    cfg = ap.parse_args()

    p1_cfg = SimpleNamespace(
        live_window=cfg.live_window, accumulation=cfg.accumulation, bucket_s=cfg.bucket_s,
        on_pps=cfg.on_pps, min_transitions=cfg.min_transitions, max_lag=cfg.max_lag,
        score_threshold=cfg.score_threshold,
    )

    rows = [summarize_n(cfg.results_dir, n, cfg.seeds, p1_cfg) for n in cfg.n_values]

    header = ["n", "bursts_total", "single_hop_recall_mean", "single_hop_recall_ci",
              "score_gap_mean", "score_gap_ci", "trace_success_rate_mean", "trace_success_rate_ci",
              "hops_reached_mean", "hops_reached_ci", "time_to_trace_mean", "time_to_trace_ci",
              "reid_precision_mean", "reid_precision_ci", "reid_recall_mean", "reid_recall_ci"]

    print(f"{'N':>6} {'success_rate':>18} {'hops_reached':>16} {'time_to_trace':>16} {'reid_recall':>16}")
    for r in rows:
        print(f"{r['n']:>6} "
              f"{r['trace_success_rate_mean']:.3f}+/-{r['trace_success_rate_ci']:.3f}   "
              f"{r['hops_reached_mean']:.2f}+/-{r['hops_reached_ci']:.2f}   "
              f"{r['time_to_trace_mean']:.2f}+/-{r['time_to_trace_ci']:.2f}s   "
              f"{r['reid_recall_mean']:.3f}+/-{r['reid_recall_ci']:.3f}")

    out_csv = cfg.out_csv or (cfg.results_dir / "sweep_summary.csv")
    with out_csv.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=header)
        writer.writeheader()
        writer.writerows(rows)
    print(f"\nWrote {out_csv}")


if __name__ == "__main__":
    main()
