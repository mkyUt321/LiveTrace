#!/usr/bin/env bash
# Links this repo's ns-3 module/scenario into an existing ns-3-dev checkout
# and configures the build. Run once (idempotent) before ./ns3 build.
set -euo pipefail

LIVETRACE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NS3_DIR="${NS3_DIR:-$HOME/ns-3-dev}"

if [[ ! -d "$NS3_DIR" ]]; then
    echo "NS3_DIR ($NS3_DIR) not found. Set NS3_DIR to your ns-3-dev checkout." >&2
    exit 1
fi

ln -sfn "$LIVETRACE_DIR/contrib/livetrace" "$NS3_DIR/contrib/livetrace"
ln -sfn "$LIVETRACE_DIR/scratch/livetrace-sim.cc" "$NS3_DIR/scratch/livetrace-sim.cc"

echo "Linked contrib/livetrace and scratch/livetrace-sim.cc into $NS3_DIR"

pushd "$NS3_DIR" >/dev/null
./ns3 configure --enable-examples --enable-tests >/dev/null
popd >/dev/null

echo "Configured ns-3. Build with: (cd $NS3_DIR && ./ns3 build)"
