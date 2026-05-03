#!/usr/bin/env bash
# run_clustering_experiments.sh
# Runs a suite of arcane-clustering-sim comparisons (RulesEngine vs AffinityEngine)
# and saves results to experiments/results/<timestamp>/.
#
# Prerequisites: run from repo root. Rust toolchain must be installed.
#
# Usage:
#   ./scripts/run_clustering_experiments.sh
#   ./scripts/run_clustering_experiments.sh --quick   # 100 ticks only, faster
#   ./scripts/run_clustering_experiments.sh --ticks 500

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BINARY="$REPO_ROOT/target/debug/arcane-clustering-sim"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
RESULTS_DIR="$REPO_ROOT/experiments/results/$TIMESTAMP"

TICKS=300
QUICK=0

for arg in "$@"; do
  case "$arg" in
    --quick) QUICK=1; TICKS=100 ;;
    --ticks) shift; TICKS="$1" ;;
    --ticks=*) TICKS="${arg#--ticks=}" ;;
  esac
done

mkdir -p "$RESULTS_DIR"

echo "=== Arcane Clustering Model Experiments ==="
echo "Timestamp : $TIMESTAMP"
echo "Ticks     : $TICKS"
echo "Results   : $RESULTS_DIR"
echo ""

# ── Build ──────────────────────────────────────────────────────────────────

echo "Building arcane-clustering-sim..."
pushd "$REPO_ROOT" > /dev/null
cargo build --bin arcane-clustering-sim --features clustering-sim -q
popd > /dev/null
echo "Build complete."
echo ""

# ── Helper ─────────────────────────────────────────────────────────────────

run_experiment() {
  local name="$1"; shift
  local extra_args=("$@")
  local outfile="$RESULTS_DIR/${name}.jsonl"
  local summfile="$RESULTS_DIR/${name}.summary.txt"

  echo "Running: $name"
  "$BINARY" --compare --ticks "$TICKS" "${extra_args[@]}" \
    > "$outfile" \
    2> "$summfile"

  # Print the FINAL lines from stderr (fragmentation summary)
  grep "^FINAL" "$summfile" | sed "s/^/  /"
  echo ""
}

# ── Experiment 1: Default — 30 agents, group_size=10, 3 zones ──────────────

echo "--- Experiment 1: Default (30 agents, group_size=10, 3 zones) ---"
run_experiment "01_default" \
  --agents 30 --group-size 10 --zones 3

# ── Experiment 2: Smaller groups — more groups, tighter cohesion ───────────

echo "--- Experiment 2: Small groups (30 agents, group_size=5, 3 zones) ---"
run_experiment "02_small_groups" \
  --agents 30 --group-size 5 --zones 3

# ── Experiment 3: Larger groups — fewer groups, more signals per group ─────

echo "--- Experiment 3: Large groups (30 agents, group_size=15, 3 zones) ---"
run_experiment "03_large_groups" \
  --agents 30 --group-size 15 --zones 3

# ── Experiment 4: More zones — higher initial fragmentation ────────────────

echo "--- Experiment 4: More zones (36 agents, group_size=9, 4 zones) ---"
run_experiment "04_more_zones" \
  --agents 36 --group-size 9 --zones 4

# ── Experiment 5: Scale — more agents ──────────────────────────────────────

if [ "$QUICK" -eq 0 ]; then
  echo "--- Experiment 5: Scale (60 agents, group_size=10, 3 zones) ---"
  run_experiment "05_scale" \
    --agents 60 --group-size 10 --zones 3
fi

# ── Summary table ───────────────────────────────────────────────────────────

echo "=== Summary ==="
printf "%-30s  %-8s  %-8s\n" "Experiment" "Rules" "Affinity"
printf "%-30s  %-8s  %-8s\n" "----------" "-----" "--------"

for summfile in "$RESULTS_DIR"/*.summary.txt; do
  name="$(basename "$summfile" .summary.txt)"
  rules=$(grep "FINAL rules" "$summfile" | grep -oE '[0-9]+\.[0-9]+' || echo "n/a")
  affinity=$(grep "FINAL affinity" "$summfile" | grep -oE '[0-9]+\.[0-9]+' || echo "n/a")
  printf "%-30s  %-8s  %-8s\n" "$name" "$rules" "$affinity"
done

echo ""
echo "Full JSON output saved to: $RESULTS_DIR"
echo ""
echo "To inspect a run:"
echo "  cat $RESULTS_DIR/01_default.jsonl | grep '\"model\":\"affinity\"'"
echo ""
echo "To plot with Python (requires matplotlib):"
echo "  python3 scripts/plot_clustering.py $RESULTS_DIR/01_default.jsonl"
