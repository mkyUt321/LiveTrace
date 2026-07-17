#!/usr/bin/env bash
# Runs the N x seed grid defined in a LiveTrace config's `sweep:` section.
# Usage: tools/run_sweep.sh [config_path] [outdir]
set -euo pipefail

LIVETRACE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NS3_DIR="${NS3_DIR:-$HOME/ns-3-dev}"
CONFIG_PATH="${1:-$LIVETRACE_DIR/config/default.yaml}"
OUT_DIR="${2:-$LIVETRACE_DIR/results}"
BIN="$NS3_DIR/build/scratch/ns3.48-livetrace-sim-debug"

if [[ ! -x "$BIN" ]]; then
    echo "Binary not found at $BIN -- run tools/setup.sh and ./ns3 build first." >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

N_VALUES=$(python3 -c "
import yaml
cfg = yaml.safe_load(open('$CONFIG_PATH'))
print(' '.join(str(v) for v in cfg['sweep']['n_values']))
")
SEEDS=$(python3 -c "
import yaml
cfg = yaml.safe_load(open('$CONFIG_PATH'))
print(' '.join(str(v) for v in cfg['sweep']['seeds']))
")

echo "Sweep grid: N in [$N_VALUES], seeds in [$SEEDS]"
total=0
for n in $N_VALUES; do
    for seed in $SEEDS; do
        total=$((total + 1))
    done
done
echo "Total runs: $total"

count=0
start_all=$(date +%s)
for n in $N_VALUES; do
    for seed in $SEEDS; do
        count=$((count + 1))
        start=$(date +%s)
        "$BIN" --config="$CONFIG_PATH" --outdir="$OUT_DIR" --seed="$seed" --n="$n" \
            > "$OUT_DIR/run_seed${seed}_n${n}.log" 2>&1 && status=0 || status=$?
        elapsed=$(( $(date +%s) - start ))
        echo "[$count/$total] n=$n seed=$seed exit=$status (${elapsed}s)"
        if [[ $status -ne 0 ]]; then
            echo "  !! run failed, see $OUT_DIR/run_seed${seed}_n${n}.log" >&2
        fi
    done
done
echo "Sweep complete in $(( $(date +%s) - start_all ))s"
