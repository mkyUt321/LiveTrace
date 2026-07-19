#!/usr/bin/env python3
"""Unit tests for the Phase 6 analysis fixes: Wilson/t-distribution CIs in
analysis/common.py, and the "missing traces count as failures, not dropped
denominator entries" fix in analysis/evaluate_phase1/2/3.py.

Run with: python3 -m unittest tests/test_analysis.py -v
(run from the repo root so `analysis` is importable, or add it to sys.path
as done below).
"""
from __future__ import annotations

import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "analysis"))

from common import RunLogs, assign_one_to_one, mean_ci95, wilson_ci  # noqa: E402
from evaluate_phase2 import evaluate_run as evaluate_phase2_run  # noqa: E402
from evaluate_phase3 import evaluate_run as evaluate_phase3_run  # noqa: E402


class WilsonCiTests(unittest.TestCase):
    def test_zero_trials_returns_nan(self):
        p, lo, hi, trials = wilson_ci(0, 0)
        self.assertTrue(math.isnan(p))
        self.assertEqual(trials, 0)

    def test_all_successes_has_nonzero_width_below_one(self):
        # 10/10 successes should NOT read as a perfectly certain 1.0 point
        # with a 0-width interval -- Wilson correctly acknowledges that 10
        # trials isn't enough to rule out a true rate somewhat below 1.0.
        p, lo, hi, trials = wilson_ci(10, 10)
        self.assertEqual(p, 1.0)
        self.assertLess(lo, 1.0)
        self.assertGreater(lo, 0.5)  # still a fairly informative lower bound
        self.assertEqual(hi, 1.0)

    def test_half_successes_centered_near_half(self):
        p, lo, hi, trials = wilson_ci(5, 10)
        self.assertAlmostEqual(p, 0.5)
        self.assertLess(lo, 0.5)
        self.assertGreater(hi, 0.5)

    def test_more_trials_narrows_interval(self):
        _, lo10, hi10, _ = wilson_ci(8, 10)
        _, lo100, hi100, _ = wilson_ci(80, 100)
        self.assertLess(hi100 - lo100, hi10 - lo10)


class MeanCiTTests(unittest.TestCase):
    def test_n1_returns_nan_width(self):
        mean, half_width, n = mean_ci95([5.0])
        self.assertEqual(mean, 5.0)
        self.assertTrue(math.isnan(half_width))
        self.assertEqual(n, 1)

    def test_t_wider_than_normal_approx_at_n5(self):
        # At n=5, t_{0.975,4} ~= 2.776 vs a normal-approx z=1.96 -- the
        # t-based half-width must be strictly larger for the same sample
        # (this is exactly the ~30% understatement the Phase 6 fix corrects).
        values = [1.0, 2.0, 3.0, 4.0, 100.0]  # some spread so se > 0
        mean, half_width, n = mean_ci95(values)
        var = sum((v - mean) ** 2 for v in values) / (n - 1)
        se = math.sqrt(var / n)
        normal_half_width = 1.96 * se
        self.assertGreater(half_width, normal_half_width)

    def test_empty_returns_nan(self):
        mean, half_width, n = mean_ci95([])
        self.assertTrue(math.isnan(mean))
        self.assertEqual(n, 0)


class AssignOneToOneTests(unittest.TestCase):
    """The Phase-6-followup fix: evaluation previously matched each burst to
    its nearest record independently, so two nearby bursts (e.g. from
    different actors in a multi-actor sweep config) could both claim the
    same trace/reid record as their evidence. assign_one_to_one enforces
    that each record is claimed by at most one burst."""

    def test_closer_burst_wins_the_shared_record(self):
        bursts = [{"start_time_s": 100.0}, {"start_time_s": 101.0}]
        records = [{"t": 100.3}]  # within tolerance of both bursts

        assigned = assign_one_to_one(bursts, records, lambda r: r["t"], tolerance=2.0)

        self.assertIs(assigned[0], records[0], "the closer burst (distance 0.3) should win the record")
        self.assertIsNone(assigned[1], "the farther burst (distance 0.7) must not also claim it")

    def test_each_burst_gets_its_own_nearby_record(self):
        bursts = [{"start_time_s": 0.0}, {"start_time_s": 100.0}]
        records = [{"t": 0.1}, {"t": 100.2}]

        assigned = assign_one_to_one(bursts, records, lambda r: r["t"], tolerance=2.0)

        self.assertIs(assigned[0], records[0])
        self.assertIs(assigned[1], records[1])

    def test_no_records_all_none(self):
        bursts = [{"start_time_s": 5.0}, {"start_time_s": 50.0}]

        assigned = assign_one_to_one(bursts, [], lambda r: r["t"], tolerance=2.0)

        self.assertEqual(assigned, [None, None])

    def test_tolerance_excludes_far_records(self):
        bursts = [{"start_time_s": 0.0}]
        records = [{"t": 10.0}]

        assigned = assign_one_to_one(bursts, records, lambda r: r["t"], tolerance=2.0)

        self.assertEqual(assigned, [None])


class MissingTraceCountsAsFailureTests(unittest.TestCase):
    """The core Phase 6 evaluation fix: a burst the online system never
    responded to must count as a failure, not silently vanish from the
    denominator (which previously inflated success/recall rates)."""

    def test_phase2_missing_trace_is_a_recorded_failure(self):
        run = RunLogs(seed=1, n=20)
        # One oracle burst exists...
        run.oracle_bursts = [{
            "burst_id": 0, "true_actor_id": 0, "start_time_s": 100.0,
            "true_chain": [5, 3, 0],
        }]
        # ...but the system produced no traceback record anywhere near it.
        run.traceback = [{
            "trace_id": 0, "burst_detect_time_s": 9999.0, "hops_so_far": 0,
            "chain_so_far": [0], "stop_reason": "no_match_above_threshold",
            "matched_flow": "", "score": -1.0, "eval_time_s": 9999.0,
        }]

        successes, hops_reached, ttt = evaluate_phase2_run(run)

        self.assertEqual(len(successes), 1, "the burst must still get one denominator entry")
        self.assertEqual(successes[0], 0, "a missing trace must count as a failure, not a success")
        self.assertEqual(hops_reached, [0])
        self.assertEqual(ttt, [])

    def test_phase3_missing_reid_record_counts_as_recall_miss(self):
        run = RunLogs(seed=1, n=20)
        run.oracle_bursts = [
            {"burst_id": 0, "true_actor_id": 0, "start_time_s": 100.0, "true_chain": [1, 0]},
            {"burst_id": 1, "true_actor_id": 0, "start_time_s": 160.0, "true_chain": [2, 0]},
        ]
        # Only the first burst got a reid record; the second (same true
        # actor) got none at all.
        run.reid = [{"trace_id": 0, "detect_time_s": 100.1, "assigned_cluster_id": 7, "score": 0.9, "chain": [0, 1]}]

        tp, fp, fn, n_bursts = evaluate_phase3_run(run)

        self.assertEqual(n_bursts, 2)
        self.assertEqual(tp, 0, "the one true-actor pair was never co-clustered (one side has no record)")
        self.assertEqual(fn, 1, "the missing burst must count as a recall miss, not be dropped")
        self.assertEqual(fp, 0)


if __name__ == "__main__":
    unittest.main()
