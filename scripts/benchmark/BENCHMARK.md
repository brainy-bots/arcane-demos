# Benchmarks: comparative value vs other options

We have **one implemented benchmark** that gives a **direct comparative value** against the main alternative. You can add more metrics or reference external numbers as needed.

---

## What we have (implemented)

| What | Where | Comparative value |
|------|--------|-------------------|
| **Arcane vs default Unreal** (client FPS at 20, 50, 100, 150, 200 entities) | `.\scripts\benchmark\run_benchmark.ps1` | **Same demo, same scene, same entity counts** — only the networking layer changes. Output: `benchmark_results.csv` + `benchmark_results.png` (Entities vs FPS). So we get a **direct comparison**: at each entity count, Arcane FPS vs Unreal FPS on the same machine. |
| **Server load visibility (Arcane)** | Same script, Arcane mode | Cluster logs **parseable stats** every 2s: `ArcaneServerStats: entities=N clusters=1 tick_ms=X`. The benchmark captures cluster stderr into `scripts/benchmark/cluster_stderr.txt`, parses it, and adds **ServerEntities**, **ServerClusters**, **ServerTickMs** (avg) to the CSV for Arcane rows. So you can show "we run N entities on 1 cluster with tick_ms ≈ X" (server load). |

**How to run (from repo root):**

```powershell
.\scripts\benchmark\run_benchmark.ps1              # Both modes, default counts
.\scripts\benchmark\run_benchmark.ps1 -Mode Unreal # Unreal only (no backend)
.\scripts\benchmark\run_benchmark.ps1 -Mode Arcane # Arcane only
```

**Comparative value:**  
- **Baseline = default Unreal replication** (listen server, replicated characters).  
- **Arcane = our library** (client gets state from Rust cluster via WebSocket).  
- **Metric = client FPS** (median over the run).  
- **Result = chart + CSV** showing “at N entities, Unreal gives X FPS, Arcane gives Y FPS” and where each mode becomes unplayable (e.g. &lt; 20 FPS). That is the comparative value vs “other options” when the other option is **default Unreal**.

---

## Other options we can compare against (references)

We don’t run their code, but we can **cite** them and **align our numbers** so the benchmark is comparable in spirit:

| Option | Typical scale / limit | Source / note |
|--------|------------------------|----------------|
| **Default Unreal replication** | Often struggles with 100+ concurrent replicated actors; “100 players in one place” can hit ~10–15 FPS on server in some tests. | Community / Epic docs; our benchmark measures **client** FPS for the same demo. |
| **Unreal Iris** | Designed for ~100 players (e.g. Fortnite BR). | Epic’s modern replication stack; we compare to **default** replication, not Iris. |
| **Maverick-style (e.g. SpatialOS)** | Often cited in the 200–400 character range, up to 1000 as a target. | DEMO_GOAL “Maverick-style scale target”; we can report “we reach N entities at acceptable FPS” and compare that to those ballpark numbers. |

So the **comparative value** we provide is:

1. **Quantified vs default Unreal** — same build, same map, same entity counts; only networking differs. Chart and CSV give a clear “Arcane vs Unreal” comparison.  
2. **Context vs industry** — we can state “at 200 entities we get X FPS” and compare that to typical “many characters” targets (e.g. 100–400) and to the fact that default Unreal often hits limits well before that.

---

## What we could add (optional)

To increase comparative value without changing the demo:

| Addition | What it would give |
|----------|---------------------|
| **Server-side metrics** | If the Rust cluster logs **tick time** or **CPU per frame**, we could add a second chart: “server cost at N entities” (Arcane backend vs Unreal listen server). That would compare **server** load, not just client FPS. |
| **Latency** | Measure round-trip or “input to visible update” for the player. Would compare “responsiveness” of Arcane vs Unreal on the same scenario. |
| **Published results** | Run the benchmark on a fixed spec (e.g. “Win64, GTX 1080, 200 entities”) and put the CSV + chart in the repo or docs. Then “other options” can be compared by running the same script and comparing numbers. |
| **Iris** | If you enable **Iris** in the Unreal project, we could add a third mode (e.g. `-UseIrisNetworking`) and have “Arcane vs default Unreal vs Iris” in the same chart. That would give a comparative value vs Epic’s newer stack as well. |

---

## Summary

- **Yes, there is a benchmark:** `.\scripts\benchmark\run_benchmark.ps1` — Arcane vs default Unreal, client FPS at 20 / 50 / 100 / 150 / 200 entities, same demo.  
- **Comparative value:** Direct comparison vs **default Unreal replication** (chart + CSV). We can also position results vs **industry ballparks** (e.g. 100–400 characters, Iris 100 players).  
- **Optional next steps:** Server metrics, latency, publishing results, adding an Iris mode, or adding a **SpacetimeDB-only** mode (see `docs/SPACETIMEDB_NETWORKING_PLAN.md`) for a three-way Arcane vs Unreal vs SpacetimeDB comparison.
