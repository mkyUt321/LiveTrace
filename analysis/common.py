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
    events: list = field(default_factory=list)           # {t, node, dir, flow, bytes}
    addr_to_node: dict = field(default_factory=dict)     # "10.0.0.1" -> node_id


def load_run(results_dir: Path, seed: int, n: int, tag_suffix: str = "") -> RunLogs:
    tag = f"seed{seed}_n{n}" + (f"_{tag_suffix}" if tag_suffix else "")
    run = RunLogs(seed=seed, n=n)

    for rec in read_jsonl(results_dir / f"oracle_{tag}.jsonl"):
        if rec.get("type") == "burst":
            run.oracle_bursts.append(rec)

    run.traceback = read_jsonl(results_dir / f"traceback_{tag}.jsonl")
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
    """Mean and half-width of a 95% CI (normal approximation -- fine for the
    seed counts used here; swap in scipy.stats.t for small-n rigor if the
    sweep ever runs with very few seeds)."""
    values = [v for v in values if v is not None]
    n = len(values)
    if n == 0:
        return (float("nan"), float("nan"), 0)
    mean = sum(values) / n
    if n == 1:
        return (mean, float("nan"), 1)
    var = sum((v - mean) ** 2 for v in values) / (n - 1)
    se = math.sqrt(var / n)
    half_width = 1.96 * se
    return (mean, half_width, n)
