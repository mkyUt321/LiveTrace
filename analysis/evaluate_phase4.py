#!/usr/bin/env python3
"""Phase 4/6: the scale-sweep main result.

Aggregates the same per-run metrics as evaluate_phase1/2/3.py, as a function
of network size N. Writes a CSV (results/sweep_summary.csv by default) that
analysis/plot_sweep.py turns into figures. A cell where the traceback never
reaches the origin at some N is not an error -- it's a real negative
observation, and (as of Phase 6) bursts the system never responded to at all
count as failures rather than being dropped from the denominator.

Two different kinds of interval are reported, deliberately not conflated:
  - Rate metrics (recall, precision, trace success rate) are discrete
    per-burst/per-pair Bernoulli trials, POOLED across all seeds and reported
    as an asymmetric Wilson 95% CI. Sample size is the trial count.
  - Continuous metrics (hops reached, time-to-trace, score gap) are reported
    as a per-seed (or per-observation) mean +/- a symmetric t-distribution
    95% CI. Sample size is the seed/observation count.
See docs/summaries/phase6 for why a normal-approximation CI over per-seed
rate averages was misleading (collapses to ~0 width whenever every seed
happens to hit 0% or 100%).

Usage:
    python3 analysis/evaluate_phase4.py --results-dir ../results \
        --n-values 20 40 80 160 320 --seeds 1 2 3 4 5
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path
from types import SimpleNamespace

from common import load_run, mean_ci95, wilson_ci
from evaluate_phase1 import evaluate_run as evaluate_phase1_run
from evaluate_phase2 import evaluate_run as evaluate_phase2_run
from evaluate_phase3 import evaluate_run as evaluate_phase3_run


def summarize_n(results_dir: Path, n: int, seeds: list[int], p1_cfg) -> dict:
    all_hits, all_prec1, all_gaps = [], [], []
    all_successes, per_seed_hops, all_ttt = [], [], []
    total_tp = total_fp = total_fn = 0
    bursts_total = 0

    for seed in seeds:
        run = load_run(results_dir, seed, n)
        bursts_total += len(run.oracle_bursts)

        hits, prec, gaps = evaluate_phase1_run(run, p1_cfg)
        all_hits += hits
        all_prec1 += prec
        all_gaps += gaps

        succ, hops, ttt = evaluate_phase2_run(run)
        all_successes += succ
        if hops:
            per_seed_hops.append(sum(hops) / len(hops))
        all_ttt += ttt

        tp, fp, fn, _ = evaluate_phase3_run(run)
        total_tp += tp
        total_fp += fp
        total_fn += fn

    row = {"n": n, "bursts_total": bursts_total}

    recall_p, recall_lo, recall_hi, recall_trials = wilson_ci(sum(all_hits), len(all_hits))
    row["single_hop_recall_p"], row["single_hop_recall_lo"], row["single_hop_recall_hi"], row["single_hop_recall_trials"] = (
        recall_p, recall_lo, recall_hi, recall_trials)

    prec1_p, prec1_lo, prec1_hi, prec1_trials = wilson_ci(sum(all_prec1), len(all_prec1))
    row["single_hop_precision_p"], row["single_hop_precision_lo"], row["single_hop_precision_hi"], row["single_hop_precision_trials"] = (
        prec1_p, prec1_lo, prec1_hi, prec1_trials)

    gap_mean, gap_ci, gap_n = mean_ci95(all_gaps)
    row["score_gap_mean"], row["score_gap_ci"], row["score_gap_n"] = gap_mean, gap_ci, gap_n

    succ_p, succ_lo, succ_hi, succ_trials = wilson_ci(sum(all_successes), len(all_successes))
    row["trace_success_rate_p"], row["trace_success_rate_lo"], row["trace_success_rate_hi"], row["trace_success_rate_trials"] = (
        succ_p, succ_lo, succ_hi, succ_trials)

    hops_mean, hops_ci, hops_n = mean_ci95(per_seed_hops)
    row["hops_reached_mean"], row["hops_reached_ci"], row["hops_reached_n"] = hops_mean, hops_ci, hops_n

    ttt_mean, ttt_ci, ttt_n = mean_ci95(all_ttt)
    row["time_to_trace_mean"], row["time_to_trace_ci"], row["time_to_trace_n"] = ttt_mean, ttt_ci, ttt_n

    reidp_p, reidp_lo, reidp_hi, reidp_trials = wilson_ci(total_tp, total_tp + total_fp)
    row["reid_precision_p"], row["reid_precision_lo"], row["reid_precision_hi"], row["reid_precision_trials"] = (
        reidp_p, reidp_lo, reidp_hi, reidp_trials)

    reidr_p, reidr_lo, reidr_hi, reidr_trials = wilson_ci(total_tp, total_tp + total_fn)
    row["reid_recall_p"], row["reid_recall_lo"], row["reid_recall_hi"], row["reid_recall_trials"] = (
        reidr_p, reidr_lo, reidr_hi, reidr_trials)

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

    header = [
        "n", "bursts_total",
        "single_hop_recall_p", "single_hop_recall_lo", "single_hop_recall_hi", "single_hop_recall_trials",
        "single_hop_precision_p", "single_hop_precision_lo", "single_hop_precision_hi", "single_hop_precision_trials",
        "score_gap_mean", "score_gap_ci", "score_gap_n",
        "trace_success_rate_p", "trace_success_rate_lo", "trace_success_rate_hi", "trace_success_rate_trials",
        "hops_reached_mean", "hops_reached_ci", "hops_reached_n",
        "time_to_trace_mean", "time_to_trace_ci", "time_to_trace_n",
        "reid_precision_p", "reid_precision_lo", "reid_precision_hi", "reid_precision_trials",
        "reid_recall_p", "reid_recall_lo", "reid_recall_hi", "reid_recall_trials",
    ]

    print(f"{'N':>6} {'success_rate [Wilson 95%]':>28} {'hops_reached':>16} {'time_to_trace':>16} {'reid_recall [Wilson 95%]':>28}")
    for r in rows:
        print(f"{r['n']:>6} "
              f"{r['trace_success_rate_p']:.3f} [{r['trace_success_rate_lo']:.3f},{r['trace_success_rate_hi']:.3f}] "
              f"{r['hops_reached_mean']:.2f}+/-{r['hops_reached_ci']:.2f}   "
              f"{r['time_to_trace_mean']:.2f}+/-{r['time_to_trace_ci']:.2f}s   "
              f"{r['reid_recall_p']:.3f} [{r['reid_recall_lo']:.3f},{r['reid_recall_hi']:.3f}]")

    out_csv = cfg.out_csv or (cfg.results_dir / "sweep_summary.csv")
    with out_csv.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=header)
        writer.writeheader()
        writer.writerows(rows)
    print(f"\nWrote {out_csv}")


if __name__ == "__main__":
    main()
