# LiveTrace

Online traceback and cross-burst re-identification for a moving-origin
periodic attacker, simulated end-to-end in ns-3.

## Central question

Can a live 5-second traceback window recover the true stepping-stone chain
back to its origin as the underlying random relay mesh grows to hundreds of
nodes? Every headline number in this repo is reported as **mean ± 95% CI
across seeds, as a function of network size N** -- including the cases where
the origin is *not* reached, which are recorded as negative observations
rather than discarded.

The oracle (true actor identity, true chain) is logged only for offline
scoring; the traceback/correlation/re-identification code never reads it.

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

See `docs/summaries/` for the per-phase parameters, metrics, and results as
they're produced.
