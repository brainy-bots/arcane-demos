# Clustering Model Experiments

How to run and interpret experiments comparing **RulesEngine (baseline)** vs **AffinityEngine (interaction-weighted)** using the `arcane-clustering-sim` binary.

---

## What we're measuring

`arcane-clustering-sim` runs two clustering models on the same group-aware agent simulation and measures **group fragmentation** — the fraction of groups whose members are spread across more than one cluster.

```
group_fragmentation = groups_with_members_in_multiple_clusters / total_groups
```

- **0.0** = all groups fully co-located (AffinityEngine target)
- **1.0** = all groups split across clusters (RulesEngine result — it has no party concept)

### Why this metric

Arcane's value proposition is: *entities that interact get placed on the same node*. Group fragmentation directly measures whether the model is achieving this. Cross-cluster RPCs happen whenever interacting entities are on different nodes; lower fragmentation = fewer cross-cluster RPCs = better performance for interacting player groups.

---

## How the simulation works

Agents are created in groups of N (each group gets a stable `party_id`). They start **round-robin** across cluster zones so every group is split from the beginning (fragmentation = 1.0). Each evaluation tick:

1. Agents tick with group cohesion (60% toward centroid) and encounter steering (periodic group-vs-group encounters)
2. The model receives `WorldStateView` with `PlayerInfo.party_id` populated from group assignments
3. `compute_entity_assignments()` returns the model's desired entity→cluster mapping
4. Fragmentation is computed from the desired mapping

**AffinityEngine**: Phase 1b injects `weight_party_member = 5.0` for every pair of agents sharing a `party_id`. After the first evaluation, the interaction graph is populated and scoring pulls group members into the same cluster. Fragmentation drops to 0 at tick 10 and stays there.

**RulesEngine**: Returns empty assignments by default (`compute_entity_assignments` default implementation). Fragmentation stays at 1.0 throughout — it has no concept of party membership.

---

## Quick start

```bash
# Build the binary (one time)
cargo build --bin arcane-clustering-sim --features clustering-sim

# Run a comparison (default: 30 agents, 3 groups of 10, 3 zones, 300 ticks)
./target/debug/arcane-clustering-sim --compare

# Run all experiment suite and save results
./scripts/run_clustering_experiments.sh

# Faster version (100 ticks)
./scripts/run_clustering_experiments.sh --quick
```

---

## Binary flags

| Flag | Default | Description |
|------|---------|-------------|
| `--model rules\|affinity` | `rules` | Run a single model |
| `--compare` | off | Run both models on the same agent sequence, interleaved output |
| `--agents N` | 30 | Total simulated agents |
| `--group-size N` | 10 | Agents per group (groups = agents / group_size) |
| `--zones N` | 3 | Initial cluster zones (agents spread round-robin) |
| `--ticks N` | 300 | Simulation length in ticks (1 tick = 50ms @ 20Hz) |
| `--eval-interval N` | 10 | Evaluate model every N ticks |

---

## Output format

One JSON line per evaluation per model:

```json
{"tick":10,"model":"affinity","clusters":3,"group_fragmentation":0.0000,"groups_co_located":3}
{"tick":10,"model":"rules",   "clusters":3,"group_fragmentation":1.0000,"groups_co_located":0}
```

Final summary on stderr:
```
FINAL rules   group_fragmentation=1.0000
FINAL affinity group_fragmentation=0.0000
```

---

## Experiments

### Experiment 1 — Default comparison (the headline result)

```bash
./target/debug/arcane-clustering-sim --compare --agents 30 --group-size 10 --zones 3 --ticks 300
```

**Expected result:**

| Tick | Rules frag | Affinity frag |
|------|-----------|---------------|
| 10 | 1.0000 | 0.0000 |
| 300 | 1.0000 | 0.0000 |

AffinityEngine co-locates all groups at the first evaluation window (tick 10). The initial 4/3/3 asymmetry is sufficient: agents in minority zones see the majority zone has more party members and all migrate there in one step.

---

### Experiment 2 — Small groups (6 groups of 5)

```bash
./target/debug/arcane-clustering-sim --compare --agents 30 --group-size 5 --zones 3
```

**Expected:** Affinity converges by tick 60, not tick 10. With 5 agents and 3 zones the initial split is 2/2/1: both "heavy" zones look equally attractive, causing a cross-zone swap at tick 10 (agents bounce, not converge). At tick 60 the sim's cooldown expires and the now-asymmetric state (3/2 after the bounce) converges cleanly.

---

### Experiment 3 — Large groups (2 groups of 15)

```bash
./target/debug/arcane-clustering-sim --compare --agents 30 --group-size 15 --zones 3
```

**Expected:** Affinity converges by tick 60. With 15 agents and 3 zones the initial split is 5/5/5 (perfectly balanced). At tick 10 agents swap between zones, creating an asymmetric 10/5 split; by tick 60 the minority-zone agents all migrate to the majority → fragmentation 0.0.

---

### Experiment 4 — More zones (4 groups of 9, 4 initial zones)

```bash
./target/debug/arcane-clustering-sim --compare --agents 36 --group-size 9 --zones 4
```

**Expected:** Affinity converges at tick 10. With 9 agents and 4 zones the split is 3/2/2/2 — zone 0 has a clear majority. All 6 agents in zones 1–3 migrate to zone 0 at the first evaluation. One-step convergence even with 4 initial zones.

---

### Experiment 5 — Scale (60 agents, 6 groups of 10)

```bash
./target/debug/arcane-clustering-sim --compare --agents 60 --group-size 10 --zones 3 --ticks 500
```

More agents, more groups. Verifies AffinityEngine scales gracefully — interaction graph is O(pairs) but GC keeps it bounded.

---

## Plotting results

```bash
# Run an experiment, save output
./target/debug/arcane-clustering-sim --compare > experiments/results/default.jsonl 2>&1

# Plot fragmentation curves
python3 scripts/plot_clustering.py experiments/results/default.jsonl

# Save plot to PNG
python3 scripts/plot_clustering.py experiments/results/default.jsonl --save
```

Requires: `pip install matplotlib`

---

## Running the full suite

```bash
./scripts/run_clustering_experiments.sh
```

Saves all results to `experiments/results/<timestamp>/` with one `.jsonl` and one `.summary.txt` per experiment. Prints a comparison table at the end:

```
=== Summary ===
Experiment                      Rules     Affinity
----------                      -----     --------
01_default                      1.0000    0.0000
02_small_groups                 1.0000    0.0000
03_large_groups                 1.0000    0.0000
04_more_zones                   1.0000    0.0000
05_scale                        1.0000    0.0000
```

All experiments show AffinityEngine converging to 0.0 and RulesEngine stuck at 1.0.

---

## What this does NOT test

- **Network throughput or latency** — for that, use `arcane-cluster-demo` + `arcane-swarm`
- **Decision execution** (physical cluster merges) — `arcane-clustering-sim` measures what the model *wants*, not what the infrastructure has applied
- **Multi-region or heterogeneous tiers** — single-region, homogeneous clusters only

The full network stack comparison (measuring replication latency under different clustering models) requires wiring ClusterManager to cluster servers — that is separate work.

---

## Key parameters for AffinityEngine tuning

These are in `AffinityConfig` (defaults shown). Vary them with a custom `SimConfig` in code or extend the binary flags:

| Parameter | Default | Sim override | Effect on fragmentation curve |
|-----------|---------|-------------|-------------------------------|
| `weight_party_member` | 5.0 | 5.0 | Higher = faster co-location |
| `migration_threshold` | 3.0 | 3.0 | Lower = more aggressive migration |
| `cooldown_ticks` | 50 | **5** | Evaluation cycles before re-migration is allowed |
| `decay_factor` | 0.97 | 0.97 | Lower = interaction history decays faster |
| `spatial_weight` | 0.2 | 0.2 | Higher = spatial proximity matters more (less relevant when party signals dominate) |

### Sim cooldown vs production cooldown

`cooldown_ticks` counts **evaluation cycles**, not simulation ticks. The production default of 50 cycles × `eval_interval=10` ticks/cycle = 500 simulation ticks of hysteresis, longer than the default 300-tick sim window. The sim uses `cooldown_ticks=5` (50 sim ticks) so that groups needing two migration rounds (experiments 2 and 3) can converge within a 300-tick run. The production value is appropriate for a real deployment where each eval cycle runs every ~50ms (20 Hz) — 50 cycles = 2.5 seconds of hysteresis to prevent thrashing.
