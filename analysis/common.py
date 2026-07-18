"""Shared log-parsing and stats helpers for LiveTrace analysis scripts.

Reads the JSONL logs produced by scratch/livetrace-sim.cc:
  oracle_*.jsonl      ground truth (evaluation-only, never fed to the system)
  observed_*.jsonl    everything the online system is allowed to see
  traceback_*.jsonl   the online TracebackObserver's own output
  topology_*.jsonl    public node <-> address map (not ground truth)
"""
from __future__ import annotations

import json
import math
from dataclasses import dataclass, field
from pathlib import Path

from scipy import stats as _scipy_stats


def read_jsonl(path: Path):
    records = []
    if not path.exists():
        return records
    with path.open() as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            records.append(json.loads(line))
    return records


@dataclass
class RunLogs:
    seed: int
    n: int
    oracle_bursts: list = field(default_factory=list)   # {burst_id, true_actor_id, start_time_s, true_chain}
    traceback: list = field(default_factory=list)       # {attempt_id, burst_detect_time_s, chain_so_far, stop_reason, matched_flow, score}
    reid: list = field(default_factory=list)             # {trace_id, detect_time_s, assigned_cluster_id, score, chain}
    events: list = field(default_factory=list)           # {t, node, dir, flow, bytes}
    addr_to_node: dict = field(default_factory=dict)     # "10.0.0.1" -> node_id


def load_run(results_dir: Path, seed: int, n: int, tag_suffix: str = "") -> RunLogs:
    tag = f"seed{seed}_n{n}" + (f"_{tag_suffix}" if tag_suffix else "")
    run = RunLogs(seed=seed, n=n)

    for rec in read_jsonl(results_dir / f"oracle_{tag}.jsonl"):
        if rec.get("type") == "burst":
            run.oracle_bursts.append(rec)

    run.traceback = read_jsonl(results_dir / f"traceback_{tag}.jsonl")
    run.reid = read_jsonl(results_dir / f"reid_{tag}.jsonl")
    run.events = read_jsonl(results_dir / f"observed_{tag}.jsonl")

    for rec in read_jsonl(results_dir / f"topology_{tag}.jsonl"):
        run.addr_to_node[rec["address"]] = rec["node_id"]

    return run


def flow_side_addr(flow_key: str, want_src: bool) -> str:
    """Undo MakeFlowKey's "src:port->dst:port" formatting."""
    src, dst = flow_key.split("->")
    side = src if want_src else dst
    addr, _, _port = side.rpartition(":")
    return addr


def mean_ci95(values):
    """Mean and half-width of a 95% CI using Student's t distribution. Use
    this for continuous-valued metrics (hops reached, time-to-trace) with a
    handful of per-seed values. Do NOT use this for pooled Bernoulli/rate
    metrics (success rate, recall, precision) -- averaging per-seed rates
    and then taking a CI across those averages collapses to a spuriously
    tight (often exactly 0-width) interval whenever every seed happens to
    hit 0% or 100%; use wilson_ci on the pooled raw trial counts instead.

    t rather than a normal (z=1.96) approximation matters here because the
    sweep runs as few as 5 seeds -- a normal approximation understates the
    true CI width by roughly 30% at n=5 (t_{0.975,4} ~= 2.776 vs z=1.96).
    """
    values = [v for v in values if v is not None]
    n = len(values)
    if n == 0:
        return (float("nan"), float("nan"), 0)
    mean = sum(values) / n
    if n == 1:
        return (mean, float("nan"), 1)
    var = sum((v - mean) ** 2 for v in values) / (n - 1)
    se = math.sqrt(var / n)
    t_crit = _scipy_stats.t.ppf(0.975, df=n - 1)
    half_width = t_crit * se
    return (mean, half_width, n)


def wilson_ci(successes: int, trials: int, z: float = 1.96):
    """Wilson score interval for a binomial proportion, computed on POOLED
    raw trial outcomes (not per-seed averages -- see mean_ci95's docstring
    for why that matters). Appropriate for rate metrics like trace success
    rate, single-hop recall, or pairwise clustering recall/precision, which
    are fundamentally counts of successes out of discrete trials rather than
    a small sample of continuous measurements.

    Returns (point_estimate, lower, upper, trials). With 0 trials, returns
    NaNs so callers can distinguish "no data" from "always succeeded".
    """
    if trials == 0:
        return (float("nan"), float("nan"), float("nan"), 0)
    phat = successes / trials
    denom = 1.0 + z * z / trials
    center = (phat + z * z / (2 * trials)) / denom
    half = (z * math.sqrt(phat * (1 - phat) / trials + z * z / (4 * trials * trials))) / denom
    lower = max(0.0, center - half)
    upper = min(1.0, center + half)
    return (phat, lower, upper, trials)
