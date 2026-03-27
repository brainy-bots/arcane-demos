#!/usr/bin/env python3
"""Read benchmark_results.csv and plot Entities vs FPS for Arcane vs Unreal mode."""
import csv
import sys
from pathlib import Path

def main():
    if len(sys.argv) < 2:
        csv_path = Path(__file__).parent / "benchmark_results.csv"
    else:
        csv_path = Path(sys.argv[1])
    if not csv_path.exists():
        print(f"No data: {csv_path}", file=sys.stderr)
        return 1

    def safe_float(s):
        if not s: return None
        try: return float(s)
        except (ValueError, TypeError): return None

    rows = []
    with open(csv_path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            rows.append({
                "Mode": r["Mode"],
                "Entities": int(r["Entities"]),
                "FPS_median": safe_float(r.get("FPS_median")),
                "FPS_min": safe_float(r.get("FPS_min")),
                "FPS_max": safe_float(r.get("FPS_max")),
            })

    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("Install matplotlib to generate chart: pip install matplotlib", file=sys.stderr)
        return 0

    arcane = sorted([r for r in rows if r["Mode"] == "Arcane" and r["FPS_median"] is not None], key=lambda x: x["Entities"])
    unreal = sorted([r for r in rows if r["Mode"] == "Unreal" and r["FPS_median"] is not None], key=lambda x: x["Entities"])
    spacetimedb = sorted([r for r in rows if r["Mode"] == "SpacetimeDB" and r["FPS_median"] is not None], key=lambda x: x["Entities"])

    fig, ax = plt.subplots()
    def plot_series(data, label, marker, style="-"):
        if not data:
            return
        x = [r["Entities"] for r in data]
        y = [r["FPS_median"] for r in data]
        ymin = [r.get("FPS_min") for r in data] if data[0].get("FPS_min") is not None else None
        ymax = [r.get("FPS_max") for r in data] if data[0].get("FPS_max") is not None else None
        if ymin and ymax:
            err_lo = [yi - (mi or yi) for yi, mi in zip(y, ymin)]
            err_hi = [(mx or yi) - yi for yi, mx in zip(y, ymax)]
            ax.errorbar(x, y, yerr=(err_lo, err_hi), label=label, capsize=3, marker=marker)
        else:
            ax.plot(x, y, marker + style, label=label)
    plot_series(arcane, "Arcane (library)", "o")
    plot_series(unreal, "Unreal (default)", "s")
    plot_series(spacetimedb, "SpacetimeDB", "^")

    ax.set_xlabel("Entity count")
    ax.set_ylabel("FPS (median)")
    ax.set_title("Arcane vs Unreal vs SpacetimeDB networking")
    ax.legend()
    ax.grid(True, alpha=0.3)
    out = csv_path.with_suffix(".png")
    fig.savefig(out, dpi=150, bbox_inches="tight")
    print(f"Chart: {out}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
