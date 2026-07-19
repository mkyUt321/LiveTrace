#!/usr/bin/env python3
"""Phase 3 evaluation: cross-burst re-identification.

Compares the online ReidentificationEngine's cluster assignments
(reid_*.jsonl) against the oracle's true_actor_id (oracle_*.jsonl, used only
for scoring) using pairwise precision/recall over all burst pairs in a run:
  precision = of burst pairs the system put in the same cluster, how many
              are truly the same actor
  recall    = of burst pairs that are truly the same actor, how many did the
              system put in the same cluster
Reported as a pooled Wilson binomial 95% CI over all pairs across all seeds
(not a mean +/- CI across per-seed pair-precision/recall averages, which is
both a small-sample-size problem and, more importantly, degenerate:

IMPORTANT: with only one true actor (attacker.count: 1, e.g. the project's
own default.yaml), every burst pair is trivially "same true actor", so no
false positive can ever be counted and precision is EXACTLY 1.0 regardless
of clustering quality -- it is not a meaningful score for the online
re-identification behavior in that configuration. This function still
computes it (so a single-actor run's output is self-consistent to
inspect), but callers evaluating clustering *quality* should use a
multi-actor config (config/phase3_reid_test.yaml) where cross-actor
confusion is actually possible, and treat single-actor precision as N/A.

A burst the online system produced no reid record for at all counts as a
failure to co-cluster (all its true-actor pairs become recall misses), not
a dropped denominator entry.
"""
from __future__ import annotations

import argparse
from itertools import combinations
from pathlib import Path

from common import assign_one_to_one, load_run, wilson_ci


def evaluate_run(run):
    # Match each oracle burst to the nearest reid record by detection time,
    # one-to-one (each reid record claimed by at most one burst -- see
    # common.assign_one_to_one for why this matters once bursts can land
    # close together in time, e.g. from different actors). A burst with no
    # matching reid record still gets an entry -- with assigned_cluster_id=
    # None, a sentinel that can never equal any real cluster id, so it
    # correctly contributes recall misses for every same-actor pair it's
    # part of instead of being silently dropped.
    assigned = assign_one_to_one(run.oracle_bursts, run.reid, lambda r: r["detect_time_s"], tolerance=2.0)
    labeled = []  # (true_actor_id, assigned_cluster_id or None)
    for burst, rec in zip(run.oracle_bursts, assigned):
        cluster_id = rec["assigned_cluster_id"] if rec is not None else None
        labeled.append((burst["true_actor_id"], cluster_id))

    tp = fp = fn = 0
    for (true_a, pred_a), (true_b, pred_b) in combinations(labeled, 2):
        same_true = true_a == true_b
        # None (missing reid record) never equals any cluster id, including
        # another None -- two non-responses are not evidence the system
        # co-clustered them.
        same_pred = pred_a is not None and pred_a == pred_b
        if same_pred and same_true:
            tp += 1
        elif same_pred and not same_true:
            fp += 1
        elif same_true and not same_pred:
            fn += 1

    return tp, fp, fn, len(labeled)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-dir", type=Path, required=True)
    ap.add_argument("--n", type=int, required=True)
    ap.add_argument("--seeds", type=int, nargs="+", required=True)
    cfg = ap.parse_args()

    total_tp = total_fp = total_fn = 0
    total_bursts = 0
    for seed in cfg.seeds:
        run = load_run(cfg.results_dir, seed, cfg.n)
        tp, fp, fn, n_bursts = evaluate_run(run)
        total_tp += tp
        total_fp += fp
        total_fn += fn
        total_bursts += n_bursts

    prec_p, prec_lo, prec_hi, prec_trials = wilson_ci(total_tp, total_tp + total_fp)
    recall_p, recall_lo, recall_hi, recall_trials = wilson_ci(total_tp, total_tp + total_fn)

    print(f"seeds evaluated: {len(cfg.seeds)}  n={cfg.n}  total bursts: {total_bursts}")
    print(f"pairwise clustering precision (pooled Wilson 95% CI): {prec_p:.3f} [{prec_lo:.3f}, {prec_hi:.3f}]  (pairs={prec_trials})")
    print(f"pairwise clustering recall (pooled Wilson 95% CI): {recall_p:.3f} [{recall_lo:.3f}, {recall_hi:.3f}]  (pairs={recall_trials})")
    print("NOTE: precision is only meaningful with >=2 true actors in this run's config "
          "(with 1 actor it is trivially 1.0 -- see this module's docstring).")


if __name__ == "__main__":
    main()
