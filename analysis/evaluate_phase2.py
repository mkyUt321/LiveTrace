#!/usr/bin/env python3
"""Phase 2 evaluation: hop-by-hop live-window traceback.

For each attack burst, compares the online TracebackObserver's fully-resolved
chain (from traceback_*.jsonl) against the oracle's true chain (from
oracle_*.jsonl, used here only for scoring) and reports:
  - trace success rate (did the chain reach the true origin, correctly, within
    the live window?) as a pooled Wilson binomial CI over every burst across
    every seed -- a burst the system never produced a matching trace for
    counts as a failure, not a dropped denominator entry (see
    docs/summaries/phase6 for why silently dropping non-responses inflated
    this rate in the original evaluation)
  - hops reached within the live window (mean +/- t-distribution 95% CI
    across seeds): for every burst, success or not -- a burst that fails to
    reach the origin is recorded as a negative observation with however many
    hops it did confirm (0 if the system produced no trace at all), not
    discarded
  - time-to-trace (successful traces only, mean +/- t-distribution 95% CI):
    how much of the live window was spent completing the traceback

Usage matches evaluate_phase1.py; see that file's docstring.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

from common import load_run, mean_ci95, wilson_ci


def evaluate_run(run):
    by_trace = defaultdict(list)
    for rec in run.traceback:
        by_trace[rec["trace_id"]].append(rec)

    # Match each oracle burst to the trace whose burst_detect_time_s is
    # closest to the burst's start_time_s (the observer has no burst_id --
    # it never sees the oracle -- so time proximity is how evaluation lines
    # them up after the fact).
    traces_by_detect_time = []
    for trace_id, recs in by_trace.items():
        recs.sort(key=lambda r: r["hops_so_far"])
        traces_by_detect_time.append((recs[0]["burst_detect_time_s"], recs))

    successes = []       # 1/0 per burst -- every oracle burst gets an entry
    hops_reached = []    # per burst, regardless of success (0 if no trace found)
    time_to_trace = []   # successful bursts only

    for burst in run.oracle_bursts:
        start = burst["start_time_s"]
        expected = list(reversed(burst["true_chain"]))  # victim-first order

        best = min(traces_by_detect_time, key=lambda t: abs(t[0] - start), default=None)
        if best is None or abs(best[0] - start) > 2.0:
            # The system produced no matching trace for this burst at all --
            # the most damning possible outcome, so it must count as a
            # failure rather than being dropped from every metric's
            # denominator.
            successes.append(0)
            hops_reached.append(0)
            continue

        recs = best[1]
        final = recs[-1]
        chain = final["chain_so_far"]

        hops_reached.append(len(chain) - 1)

        success = chain == expected
        successes.append(1 if success else 0)
        if success:
            time_to_trace.append(final["eval_time_s"] - start)

    return successes, hops_reached, time_to_trace


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-dir", type=Path, required=True)
    ap.add_argument("--n", type=int, required=True)
    ap.add_argument("--seeds", type=int, nargs="+", required=True)
    cfg = ap.parse_args()

    per_seed_mean_hops = []
    all_time_to_trace = []
    pooled_successes = 0
    pooled_trials = 0

    for seed in cfg.seeds:
        run = load_run(cfg.results_dir, seed, cfg.n)
        successes, hops, ttt = evaluate_run(run)
        pooled_successes += sum(successes)
        pooled_trials += len(successes)
        if hops:
            per_seed_mean_hops.append(sum(hops) / len(hops))
        all_time_to_trace += ttt

    succ_p, succ_lo, succ_hi, succ_trials = wilson_ci(pooled_successes, pooled_trials)
    hops_mean, hops_ci, hops_n = mean_ci95(per_seed_mean_hops)
    ttt_mean, ttt_ci, ttt_n = mean_ci95(all_time_to_trace)

    print(f"seeds evaluated: {len(cfg.seeds)}  n={cfg.n}  total bursts: {pooled_trials}")
    print(f"trace success rate (reached true origin, pooled Wilson 95% CI): "
          f"{succ_p:.3f} [{succ_lo:.3f}, {succ_hi:.3f}]  (trials={succ_trials})")
    print(f"hops reached within live window (per-seed mean +/- t-dist 95% CI): {hops_mean:.3f} +/- {hops_ci:.3f}  (seeds={hops_n})")
    print(f"time-to-trace, successful only (mean +/- t-dist 95% CI): {ttt_mean:.3f}s +/- {ttt_ci:.3f}s  (n={ttt_n})")


if __name__ == "__main__":
    main()
