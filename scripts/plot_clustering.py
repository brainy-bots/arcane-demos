#!/usr/bin/env python3
"""
plot_clustering.py — plot group fragmentation over time from arcane-clustering-sim output.

Usage:
    python3 scripts/plot_clustering.py experiments/results/<timestamp>/01_default.jsonl
    python3 scripts/plot_clustering.py experiments/results/<timestamp>/01_default.jsonl --save

Reads the interleaved JSON lines (--compare output) and plots fragmentation
curves for both models on the same axes.

Requires: matplotlib (pip install matplotlib)
"""

import json
import sys
from pathlib import Path


def load(path: str):
    rules_ticks, rules_frag = [], []
    affinity_ticks, affinity_frag = [], []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                d = json.loads(line)
            except json.JSONDecodeError:
                continue
            if d.get("model") == "rules":
                rules_ticks.append(d["tick"])
                rules_frag.append(d["group_fragmentation"])
            elif d.get("model") == "affinity":
                affinity_ticks.append(d["tick"])
                affinity_frag.append(d["group_fragmentation"])
    return rules_ticks, rules_frag, affinity_ticks, affinity_frag


def main():
    if len(sys.argv) < 2:
        print("Usage: plot_clustering.py <path.jsonl> [--save]")
        sys.exit(1)

    path = sys.argv[1]
    save = "--save" in sys.argv

    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not installed. Run: pip install matplotlib")
        sys.exit(1)

    rules_ticks, rules_frag, affinity_ticks, affinity_frag = load(path)

    fig, ax = plt.subplots(figsize=(10, 5))
    ax.plot(rules_ticks, rules_frag, label="RulesEngine (baseline)", color="#e05555", linewidth=2)
    ax.plot(affinity_ticks, affinity_frag, label="AffinityEngine (weighted)", color="#4a9e4a", linewidth=2)

    ax.set_xlabel("Tick", fontsize=12)
    ax.set_ylabel("Group Fragmentation", fontsize=12)
    ax.set_title("Clustering Model Comparison — Group Fragmentation Over Time", fontsize=13)
    ax.set_ylim(-0.05, 1.05)
    ax.axhline(0, color="gray", linewidth=0.5, linestyle="--")
    ax.axhline(1, color="gray", linewidth=0.5, linestyle="--")
    ax.legend(fontsize=11)
    ax.grid(True, alpha=0.3)

    name = Path(path).stem
    fig.tight_layout()

    if save:
        out = Path(path).with_suffix(".png")
        plt.savefig(out, dpi=150)
        print(f"Saved: {out}")
    else:
        plt.show()


if __name__ == "__main__":
    main()
