"""Python mirror of contrib/livetrace/model/timing-correlator.cc.

Used only for offline evaluation (e.g. to see the full candidate score
distribution, not just the online system's winning match) -- never by the
online system itself, which runs the real C++ implementation. Kept in sync
by hand; if you change the on/off bucketing or the correlation score in the
C++ side, mirror the change here.
"""
from __future__ import annotations

import math


def compute_on_off_signal(events, flow_key: str, t0: float, t1: float, bucket_s: float, on_threshold_pps: float):
    n_buckets = int(math.ceil((t1 - t0) / bucket_s)) + 1
    counts = [0.0] * n_buckets
    for e in events:
        if e["flow"] != flow_key:
            continue
        t = e["t"]
        if t < t0 or t > t1:
            continue
        idx = int((t - t0) / bucket_s)
        if 0 <= idx < n_buckets:
            counts[idx] += 1.0

    on_count_threshold = max(1.0, on_threshold_pps * bucket_s)
    return [1 if c >= on_count_threshold else 0 for c in counts]


def count_transitions(signal):
    return sum(1 for i in range(1, len(signal)) if signal[i] != signal[i - 1])


def _pearson_at_lag(a, b, lag):
    n, m = len(a), len(b)
    start = max(0, -lag)
    end = min(n, m - lag)
    count = end - start
    if count < 2:
        return -1.0
    mean_a = sum(a[start:end]) / count
    mean_b = sum(b[start + lag:end + lag]) / count
    cov = var_a = var_b = 0.0
    for i in range(start, end):
        da = a[i] - mean_a
        db = b[i + lag] - mean_b
        cov += da * db
        var_a += da * da
        var_b += db * db
    if var_a <= 0.0 or var_b <= 0.0:
        return -1.0
    return cov / math.sqrt(var_a * var_b)


def correlate(a, b, min_on_off_transitions: int, max_lag_buckets: int) -> float:
    if count_transitions(a) < min_on_off_transitions or count_transitions(b) < min_on_off_transitions:
        return -1.0
    best = -1.0
    for lag in range(-max_lag_buckets, max_lag_buckets + 1):
        best = max(best, _pearson_at_lag(a, b, lag))
    return best
