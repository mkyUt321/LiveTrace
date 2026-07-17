# LiveTrace

Online traceback and cross-burst re-identification for a moving-origin
periodic attacker, simulated end-to-end in ns-3. Built from scratch (no code
carried over from the earlier J2Trace project).

## Central question

Can a live 5-second traceback window recover the true stepping-stone chain
back to its origin as the underlying random relay mesh grows to hundreds of
nodes? Every headline number in this repo is reported as **mean ± 95% CI
across seeds, as a function of network size N** -- including the cases where
the origin is *not* reached, which are recorded as negative observations
rather than discarded.

The oracle (true actor identity, true chain) is logged only for offline
scoring; the traceback/correlation/re-identification code never reads it.

**Current headline result** (`docs/summaries/phase4.md`, N=20..320, 5 seeds
each, chain length 2-5, 5s window): trace success rate is 1.000 ± 0.000 at
every N tested -- the central difficulty does not bite yet in this parameter
regime. That's a genuine boundary observation, not a limitation being
glossed over: chain length is capped independent of N and the window budget
(~5-6 hops) comfortably covers it. See the summary for what parameter axis
would need to move to expose the actual difficulty.

## Environment (pinned for this project)

- ns-3-dev **3.48** (debug build, runtime asserts + logging on), built at
  `~/ns-3-dev` and reused via symlink (see `tools/setup.sh`) rather than
  vendored into this repo.
- WSL Ubuntu 24.04, g++ 13.3, cmake 3.28.
- Python 3.12, numpy 2.5, scipy 1.18, matplotlib 3.11 (`analysis/`).
- Visualization: NetAnim (qualitative, driven by confirmed traceback events)
  + matplotlib (quantitative scale-sweep figures). No custom HTML/JS.

## Repository layout

```
config/default.yaml       All experiment parameters (topology, attacker,
                           correlation, sweep) in one place.
contrib/livetrace/        ns-3 module: topology, attacker, relay, background
                           traffic, oracle logger, observation log,
                           correlator, traceback observer, re-id engine.
scratch/livetrace-sim.cc  Simulation driver (reads config, runs one seed).
analysis/                 Python: log parsing, metrics, scale-sweep harness,
                           figures.
tools/setup.sh            Symlinks contrib/scratch into ns-3-dev and
                           configures the build.
tools/run_sweep.sh         Runs the N x seed grid.
results/                  Raw JSONL logs + figures (gitignored; regenerate
                           via the commands below).
docs/summaries/            Per-phase Markdown summaries for transcription
                           into the project's Notion log.
```

## Reproducing a single run

```bash
export NS3_DIR=~/ns-3-dev        # wherever your ns-3-dev checkout lives
./tools/setup.sh                  # symlink + configure (idempotent)
cd "$NS3_DIR"
./ns3 build livetrace scratch/livetrace-sim
./ns3 run "scratch/livetrace-sim --config=$OLDPWD/config/default.yaml --outdir=$OLDPWD/results --seed=1"
```

This writes `results/oracle_seed1_n50.jsonl` (ground truth, evaluation-only),
`results/observed_seed1_n50.jsonl` (everything the traceback stack is allowed
to see), and `results/netanim_seed1_n50.xml` (open in NetAnim).

Run the ns-3 unit tests for this module:

```bash
cd "$NS3_DIR" && ./test.py -s livetrace
```

## Reproducing the scale sweep (Phase 4's main result)

```bash
tools/run_sweep.sh config/default.yaml results   # ~40 min for the default 5x5 grid
python3 analysis/evaluate_phase4.py --results-dir results \
    --n-values 20 40 80 160 320 --seeds 1 2 3 4 5
python3 analysis/plot_sweep.py --csv results/sweep_summary.csv --out-dir results
```

This writes `results/sweep_summary.csv` and `results/scale_sweep.png` (mean ±
95% CI, four panels, N on a log axis). Open any `results/netanim_*.xml` in
NetAnim to watch the traceback observer highlight confirmed hops live as it
runs -- red for a hop the online correlator just confirmed, blue for the
victim.

## Design decisions and why

- **Topology: Erdős–Rényi G(n,p), fixed as the sole generator.** Edge density
  is controlled independently of node count, which is what makes N a clean
  first-class scale axis for the sweep. Generation retries (bounded) until
  the mesh is connected and free of a single articulation point that would
  strand a large fraction of the network behind it -- this operationalizes
  "no bottleneck all traffic must pass through" without requiring a stronger
  (and harder to reason about) k-connectivity guarantee.
- **Stepping stones are an application-layer overlay**, not IP hops: the
  attacker picks k relay *nodes* out of the mesh and relays UDP through them;
  the physical path between any two overlay hops may itself cross several
  ns-3 point-to-point links via ordinary IP routing. This matches the
  classical Zhang–Paxson stepping-stone model the correlator implements.
- **Flow keys are built only from locally-visible address:port pairs**, never
  from burst/actor/chain identifiers, so the correlator can't take a shortcut
  -- it has to do real timing correlation to link an inbound and an outbound
  flow at a relay.
- **Oracle/observation separation is architectural, not a convention**:
  `OracleLogger` is constructed only in the simulation driver and handed
  exclusively to the attacker campaign; no other class (relay, background
  traffic, correlator, observer, re-id engine) is ever given a pointer to it.
- **Nix-vector routing, not `Ipv4GlobalRoutingHelper`.** Global routing
  precomputes all-pairs routes and is well known not to scale; it made
  N=160 take longer than 5 minutes just to set up. Nix-vector computes
  routes on demand and made the full N=320 sweep cell tractable (~7.5 min
  including the whole 900s simulated run, not just setup).
- **Background traffic rate scales with N** (`background.rate_pps` is
  per-node, multiplied by N in the driver) so ambient noise density per node
  -- and thus the correlator's candidate pool size -- stays comparable
  across the sweep instead of thinning out as the same fixed aggregate rate
  spreads over more nodes.

See `docs/summaries/` for the per-phase parameters, metrics, and results as
they're produced.
