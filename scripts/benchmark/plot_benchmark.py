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

    fig, ax = plt.subplots()
    if arcane:
        x = [r["Entities"] for r in arcane]
        y = [r["FPS_median"] for r in arcane]
        ymin = [r["FPS_min"] for r in arcane] if arcane[0].get("FPS_min") else None
        ymax = [r["FPS_max"] for r in arcane] if arcane[0].get("FPS_max") else None
        if ymin and ymax:
            err_lo = [yi - mi for yi, mi in zip(y, ymin)]
            err_hi = [mx - yi for yi, mx in zip(y, ymax)]
            ax.errorbar(x, y, yerr=(err_lo, err_hi), label="Arcane (library)", capsize=3, marker="o")
        else:
            ax.plot(x, y, "o-", label="Arcane (library)")
    if unreal:
        x = [r["Entities"] for r in unreal]
        y = [r["FPS_median"] for r in unreal]
        ymin = [r["FPS_min"] for r in unreal] if unreal[0].get("FPS_min") else None
        ymax = [r["FPS_max"] for r in unreal] if unreal[0].get("FPS_max") else None
        if ymin and ymax:
            err_lo = [yi - mi for yi, mi in zip(y, ymin)]
            err_hi = [mx - yi for yi, mx in zip(y, ymax)]
            ax.errorbar(x, y, yerr=(err_lo, err_hi), label="Unreal (default)", capsize=3, marker="s")
        else:
            ax.plot(x, y, "s-", label="Unreal (default)")

    ax.set_xlabel("Entity count")
    ax.set_ylabel("FPS (median)")
    ax.set_title("Arcane vs default Unreal networking")
    ax.legend()
    ax.grid(True, alpha=0.3)
    out = csv_path.with_suffix(".png")
    fig.savefig(out, dpi=150, bbox_inches="tight")
    print(f"Chart: {out}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
