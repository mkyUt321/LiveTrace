#!/usr/bin/env python3
"""Phase 1 evaluation: single-hop traceback accuracy and true-vs-noise score
separation, aggregated as mean +/- 95% CI across seeds.

Oracle ground truth is used here ONLY for scoring, exactly as the project's
evaluation methodology requires -- the online system (scratch/livetrace-sim,
contrib/livetrace/model/{timing-correlator,traceback-observer}.cc) never
reads it.

Usage:
    python3 analysis/evaluate_phase1.py --results-dir ../results \
        --n 12 --seeds 1 2 3 4 5 --live-window 5.0 --accumulation 2.0 \
        --bucket-s 0.05 --on-threshold-pps 1.0 --min-transitions 3 \
        --max-lag-buckets 3 --score-threshold 0.5
"""
from __future__ import annotations

import argparse
from pathlib import Path

from common import flow_side_addr, load_run, mean_ci95
from correlator import compute_on_off_signal, correlate

VICTIM_PORT = 9999


def evaluate_run(run, cfg):
    online_hits = []       # 1/0 per burst-with-true-upstream
    online_precisions = [] # 1/0 per "matched" output
    score_gaps = []         # true_score - best_false_score, when both exist

    for burst in run.oracle_bursts:
        chain = burst["true_chain"]
        start = burst["start_time_s"]
        victim_node = chain[-1]
        expected_upstream = chain[-3] if len(chain) >= 3 else None
        r_node_true = chain[-2]

        # Find the confirmed victim-inbound flow for this burst.
        confirmed_flow = None
        confirmed_events = [
            e for e in run.events
            if e["node"] == victim_node and e["dir"] == "rx" and start <= e["t"] <= start + cfg.live_window
            and e["flow"].endswith(f":{VICTIM_PORT}")
        ]
        if not confirmed_events:
            continue
        confirmed_events.sort(key=lambda e: e["t"])
        confirmed_flow = confirmed_events[0]["flow"]

        r_addr = flow_side_addr(confirmed_flow, want_src=True)
        r_node = run.addr_to_node.get(r_addr)
        if r_node != r_node_true:
            # Address resolution mismatch would indicate a topology/log bug;
            # skip rather than silently mis-score.
            continue

        # Offline: full candidate score distribution at R.
        window_end = start + cfg.accumulation
        candidate_flows = sorted({
            e["flow"] for e in run.events
            if e["node"] == r_node and e["dir"] == "rx" and start <= e["t"] <= window_end
            and e["flow"] != confirmed_flow
        })
        ref_signal = compute_on_off_signal(run.events, confirmed_flow, start, window_end, cfg.bucket_s, cfg.on_pps)

        true_score = None
        false_scores = []
        for cflow in candidate_flows:
            cand_signal = compute_on_off_signal(run.events, cflow, start, window_end, cfg.bucket_s, cfg.on_pps)
            score = correlate(ref_signal, cand_signal, cfg.min_transitions, cfg.max_lag)
            caddr = flow_side_addr(cflow, want_src=True)
            cnode = run.addr_to_node.get(caddr)
            if expected_upstream is not None and cnode == expected_upstream:
                true_score = score
            else:
                false_scores.append(score)

        if true_score is not None and false_scores:
            score_gaps.append(true_score - max(false_scores))

        # Online: what the real system actually reported.
        attempt = min(
            run.traceback,
            key=lambda a: abs(a["burst_detect_time_s"] - start),
            default=None,
        )
        if attempt is None or abs(attempt["burst_detect_time_s"] - start) > cfg.live_window:
            continue

        matched = attempt["stop_reason"] == "matched"
        resolved_node = run.addr_to_node.get(flow_side_addr(attempt["matched_flow"], True)) if matched else None

        if expected_upstream is not None:
            online_hits.append(1 if (matched and resolved_node == expected_upstream) else 0)
        if matched:
            online_precisions.append(1 if resolved_node == expected_upstream else 0)

    return online_hits, online_precisions, score_gaps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-dir", type=Path, required=True)
    ap.add_argument("--n", type=int, required=True)
    ap.add_argument("--seeds", type=int, nargs="+", required=True)
    ap.add_argument("--live-window", type=float, default=5.0)
    ap.add_argument("--accumulation", type=float, default=2.0)
    ap.add_argument("--bucket-s", type=float, default=0.05)
    ap.add_argument("--on-threshold-pps", dest="on_pps", type=float, default=1.0)
    ap.add_argument("--min-transitions", type=int, default=3)
    ap.add_argument("--max-lag-buckets", dest="max_lag", type=int, default=3)
    ap.add_argument("--score-threshold", type=float, default=0.5)
    cfg = ap.parse_args()

    all_hits, all_prec, all_gaps = [], [], []
    per_seed_recall = []
    for seed in cfg.seeds:
        run = load_run(cfg.results_dir, seed, cfg.n)
        hits, prec, gaps = evaluate_run(run, cfg)
        all_hits += hits
        all_prec += prec
        all_gaps += gaps
        if hits:
            per_seed_recall.append(sum(hits) / len(hits))

    recall_mean, recall_ci, recall_n = mean_ci95(per_seed_recall)
    gap_mean, gap_ci, gap_n = mean_ci95(all_gaps)
    precision = (sum(all_prec) / len(all_prec)) if all_prec else float("nan")

    print(f"seeds evaluated: {len(cfg.seeds)}  n={cfg.n}")
    print(f"bursts with a true upstream hop: {len(all_hits)}  'matched' outputs: {len(all_prec)}")
    print(f"single-hop recall (per-seed mean +/- 95% CI): {recall_mean:.3f} +/- {recall_ci:.3f}  (seeds={recall_n})")
    print(f"single-hop precision (pooled): {precision:.3f}  ({sum(all_prec)}/{len(all_prec)})")
    print(f"true-vs-best-noise score gap (mean +/- 95% CI): {gap_mean:.3f} +/- {gap_ci:.3f}  (n={gap_n})")


if __name__ == "__main__":
    main()
