#!/usr/bin/env python3
"""Phase 3 evaluation: cross-burst re-identification.

Compares the online ReidentificationEngine's cluster assignments
(reid_*.jsonl) against the oracle's true_actor_id (oracle_*.jsonl, used only
for scoring) using pairwise precision/recall over all burst pairs in a run:
  precision = of burst pairs the system put in the same cluster, how many
              are truly the same actor
  recall    = of burst pairs that are truly the same actor, how many did the
              system put in the same cluster
Reported as mean +/- 95% CI across seeds.
"""
from __future__ import annotations

import argparse
from itertools import combinations
from pathlib import Path

from common import load_run, mean_ci95


def evaluate_run(run):
    # Match each oracle burst to the nearest reid record by detection time.
    labeled = []  # (true_actor_id, assigned_cluster_id)
    for burst in run.oracle_bursts:
        start = burst["start_time_s"]
        best = min(run.reid, key=lambda r: abs(r["detect_time_s"] - start), default=None)
        if best is None or abs(best["detect_time_s"] - start) > 2.0:
            continue
        labeled.append((burst["true_actor_id"], best["assigned_cluster_id"]))

    tp = fp = fn = 0
    for (true_a, pred_a), (true_b, pred_b) in combinations(labeled, 2):
        same_true = true_a == true_b
        same_pred = pred_a == pred_b
        if same_pred and same_true:
            tp += 1
        elif same_pred and not same_true:
            fp += 1
        elif same_true and not same_pred:
            fn += 1

    precision = tp / (tp + fp) if (tp + fp) > 0 else float("nan")
    recall = tp / (tp + fn) if (tp + fn) > 0 else float("nan")
    return precision, recall, len(labeled)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-dir", type=Path, required=True)
    ap.add_argument("--n", type=int, required=True)
    ap.add_argument("--seeds", type=int, nargs="+", required=True)
    cfg = ap.parse_args()

    precisions, recalls = [], []
    total_bursts = 0
    for seed in cfg.seeds:
        run = load_run(cfg.results_dir, seed, cfg.n)
        p, r, n_bursts = evaluate_run(run)
        if p == p:  # not NaN
            precisions.append(p)
        if r == r:
            recalls.append(r)
        total_bursts += n_bursts

    p_mean, p_ci, p_n = mean_ci95(precisions)
    r_mean, r_ci, r_n = mean_ci95(recalls)

    print(f"seeds evaluated: {len(cfg.seeds)}  n={cfg.n}  total bursts: {total_bursts}")
    print(f"pairwise clustering precision (per-seed mean +/- 95% CI): {p_mean:.3f} +/- {p_ci:.3f}  (seeds={p_n})")
    print(f"pairwise clustering recall (per-seed mean +/- 95% CI): {r_mean:.3f} +/- {r_ci:.3f}  (seeds={r_n})")


if __name__ == "__main__":
    main()
