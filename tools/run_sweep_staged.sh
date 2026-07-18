#!/usr/bin/env bash
# Staged version of run_sweep.sh: runs N<=320 at a full seed count and N=640
# (much slower per seed -- see config/scale_sweep.yaml's comments) at a
# reduced seed count, so the whole grid finishes in a practical amount of
# time. All output lands in the same results dir; filenames are already
# namespaced by seed+N so the two tiers coexist without collision.
#
# Usage: tools/run_sweep_staged.sh [config_path] [outdir]
set -euo pipefail

LIVETRACE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NS3_DIR="${NS3_DIR:-$HOME/ns-3-dev}"
CONFIG_PATH="${1:-$LIVETRACE_DIR/config/scale_sweep.yaml}"
OUT_DIR="${2:-$LIVETRACE_DIR/results}"
BIN="$NS3_DIR/build/scratch/ns3.48-livetrace-sim-debug"

if [[ ! -x "$BIN" ]]; then
    echo "Binary not found at $BIN -- run tools/setup.sh and ./ns3 build first." >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

MAIN_N_VALUES="20 40 80 160 320"
MAIN_SEEDS="1 2 3 4 5 6 7 8"
LARGE_N_VALUES="640"
LARGE_SEEDS="1 2 3"

run_cell() {
    local n=$1 seed=$2
    local start=$(date +%s)
    "$BIN" --config="$CONFIG_PATH" --outdir="$OUT_DIR" --seed="$seed" --n="$n" \
        > "$OUT_DIR/run_seed${seed}_n${n}.log" 2>&1 && status=0 || status=$?
    local elapsed=$(( $(date +%s) - start ))
    echo "n=$n seed=$seed exit=$status (${elapsed}s)"
    if [[ $status -ne 0 ]]; then
        echo "  !! run failed, see $OUT_DIR/run_seed${seed}_n${n}.log" >&2
    fi
}

echo "Stage 1: N in [$MAIN_N_VALUES], seeds in [$MAIN_SEEDS]"
count=0
start_all=$(date +%s)
for n in $MAIN_N_VALUES; do
    for seed in $MAIN_SEEDS; do
        count=$((count + 1))
        run_cell "$n" "$seed"
    done
done
echo "Stage 1 complete: $count runs in $(( $(date +%s) - start_all ))s"

echo "Stage 2: N in [$LARGE_N_VALUES], seeds in [$LARGE_SEEDS]"
count=0
start_stage2=$(date +%s)
for n in $LARGE_N_VALUES; do
    for seed in $LARGE_SEEDS; do
        count=$((count + 1))
        run_cell "$n" "$seed"
    done
done
echo "Stage 2 complete: $count runs in $(( $(date +%s) - start_stage2 ))s"
echo "Staged sweep complete in $(( $(date +%s) - start_all ))s total"
