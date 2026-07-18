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

**Current headline result** (`docs/summaries/phase6.md`, N=20..640,
`config/scale_sweep.yaml`): trace success rate falls from **1.000 at N=20-40
to 0.325 [0.248, 0.413] at N=640**, a real, statistically-supported
difficulty curve. Single-hop recall stays at 1.000 across every N, so the
degradation is squarely a live-window budget problem (hops reached and
time-to-trace both climb steadily with N), not a correlation failure --
mesh diameter is held to scale with N (fixed average node degree, not fixed
edge probability) and stepping-stone chain length is tied to that diameter,
which is what makes N an axis the difficulty actually responds to. An
earlier sweep (`docs/summaries/phase4.md`) found a flat 100% success rate at
every N; that turned out to be an experimental-design artifact (chain length
was a fixed constant independent of N) rather than evidence the system is
robust at scale -- see `docs/summaries/phase6.md` for the full audit and fix.

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
config/default.yaml       Single-actor, canonical-420s-period baseline (matches
                           the literal spec); also the single-run example config.
config/scale_sweep.yaml   Main-result sweep config: 2 actors, fixed avg degree
                           (diameter grows with N), compressed period for
                           statistical power. See docs/summaries/phase6.md.
config/phase3_reid_test.yaml  Dedicated 2-actor re-identification test config
                           (makes reid_precision non-degenerate).
contrib/livetrace/        ns-3 module: topology, attacker, relay, background
                           traffic, oracle logger, observation log,
                           correlator, traceback observer, re-id engine.
scratch/livetrace-sim.cc  Simulation driver (reads config, runs one seed).
analysis/                 Python: log parsing, metrics, scale-sweep harness,
                           figures.
tests/                    Python unit tests for the analysis/ statistics helpers.
tools/setup.sh            Symlinks contrib/scratch into ns-3-dev and
                           configures the build.
tools/run_sweep.sh        Runs a simple N x seed grid (one seed count for all N).
tools/run_sweep_staged.sh Runs the main sweep with fewer seeds at large N
                           (large N is much slower per seed).
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

## Reproducing the scale sweep (main result)

```bash
tools/run_sweep_staged.sh config/scale_sweep.yaml results   # ~50 min: N<=320 at 8 seeds, N=640 at 3
python3 analysis/evaluate_phase4.py --results-dir results \
    --n-values 20 40 80 160 320 --seeds 1 2 3 4 5 6 7 8 --out-csv /tmp/sweep_stage1.csv
python3 analysis/evaluate_phase4.py --results-dir results \
    --n-values 640 --seeds 1 2 3 --out-csv /tmp/sweep_stage2.csv
head -1 /tmp/sweep_stage1.csv > results/sweep_summary.csv
tail -n +2 /tmp/sweep_stage1.csv >> results/sweep_summary.csv
tail -n +2 /tmp/sweep_stage2.csv >> results/sweep_summary.csv
python3 analysis/plot_sweep.py --csv results/sweep_summary.csv --out-dir results
```

(`config/default.yaml` + `tools/run_sweep.sh` still work for a simpler,
single-actor, canonical-420s-period, fixed-p sweep -- that's what
`docs/summaries/phase4.md` used, before the Phase 6 fixes below.)

This writes `results/sweep_summary.csv` and `results/scale_sweep.png`
(Wilson 95% CI for rate metrics, t-distribution 95% CI for continuous
metrics, four panels, N on a log axis, each point annotated with its sample
size). Open any `results/netanim_*.xml` in NetAnim to watch the traceback
observer highlight confirmed hops live as it runs -- red for a hop the
online correlator just confirmed, blue for the victim.

Reproduce the Phase 3 two-actor re-identification test (needed for a
meaningful `reid_precision`, which is trivially 1.0 with only one true
actor):

```bash
./ns3 run "scratch/livetrace-sim --config=$LIVETRACE_DIR/config/phase3_reid_test.yaml --outdir=$LIVETRACE_DIR/results --seed=1 --n=20"
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
- **Topology uses fixed average node degree, not fixed edge probability**
  (`topology.avg_degree`, deriving `p = avg_degree/(n-1)`). With fixed p the
  mesh gets relatively denser as N grows and diameter stays ~flat (2-3 hops
  even at N=320); fixed degree instead lets diameter grow (~log N) with N,
  which is what stepping-stone chain length (`attacker.chain_length_factor
  * diameter`) is tied to -- see `docs/summaries/phase6.md` for why this is
  the single most load-bearing fix behind the current headline result.
- **Rate metrics (recall, precision, trace success rate) use a pooled
  Wilson binomial CI over raw trial outcomes**, not a mean +/- CI across
  per-seed rate averages. The latter collapses to a misleadingly tight
  (often exactly 0-width) interval whenever every seed happens to land on
  0% or 100% -- which is exactly what an early, buggy sweep showed. A burst
  the online system never produced a matching trace for counts as a
  failure, not a silently dropped denominator entry.

See `docs/summaries/` for the per-phase parameters, metrics, and results as
they're produced -- `phase6.md` in particular documents a full audit of the
issues above, run when the initial Phase 4 sweep produced a suspicious flat
100% result.
