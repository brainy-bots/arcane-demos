# Scaling experiment results

Canonical parameters (all runs): **tick_rate=10 Hz**, **aps=2**, **duration=30 s**, **mode=spread**, **everyone_sees_everyone**, **demo_entities=0**. Pass criteria: **err_rate < 1%**, **lat_avg_ms < 200**.

---

## 1. SpacetimeDB only (no Arcane)

Single SpacetimeDB module; physics and persistence in the module.

| players | total_calls | total_oks | total_errs | err_rate_pct | lat_avg_ms | pass |
|--------|-------------|-----------|------------|--------------|------------|------|
| 250    | 93,047      | 78,976    | 0          | 0            | 5.87       | Yes  |
| 500    | 192,308     | 163,270   | 0          | 0            | 91.68      | Yes  |
| 750    | 302,546     | 256,758   | 0          | 0            | 68.28      | Yes  |
| 1000   | 418,164     | 354,805   | 0          | 0            | 183.61     | Yes  |
| 1250   | 542,789     | 460,476   | 0          | 0            | **688.35**  | **No** (latency) |

**Ceiling: 1000 players** (1250 fails on latency).

---

## 2. Arcane + SpacetimeDB (with persist batch cap = 500)

Arcane clusters run physics and replicate via Redis; each cluster persists to SpacetimeDB at 1 Hz. **SPACETIMEDB_PERSIST_BATCH_SIZE=500** (multiple HTTP requests per persist window). Results ordered by best: pass first, then players descending, then err_rate ascending.

| clusters | players | total_calls | total_errs | err_rate_pct | lat_avg_ms | pass |
|----------|---------|-------------|------------|--------------|------------|------|
| 4        | 4000    | 432,358     | 338        | 0.08         | 2.41       | Yes  |
| 4        | 3000    | 753,255     | 406        | 0.05         | 0.02       | Yes  |
| 3        | 3000    | 784,846     | 143        | 0.02         | 0.04       | Yes  |
| 2        | 3000    | 273,822     | 1,822      | 0.67         | 0.61       | Yes  |
| 2        | 2500    | 579,736     | 377        | 0.07         | 0.04       | Yes  |
| 3        | 2500    | 724,447     | 0          | 0            | 0.02       | Yes  |
| 2        | 2000    | 308,479     | 924        | 0.30         | 0.08       | Yes  |
| 3        | 2000    | 588,790     | 0          | 0            | 0.02       | Yes  |
| 2        | 1500    | 372,193     | 126        | 0.03         | 0.02       | Yes  |
| 1        | 1750    | 275,487     | 795        | 0.29         | 0.01       | Yes  |
| 1        | 1500    | 338,541     | 0          | 0            | 0.01       | Yes  |
| 2        | 1000    | 295,200     | 0          | 0            | 0.01       | Yes  |
| 1        | 1250    | 295,617     | 68         | 0.02         | 0.01       | Yes  |
| 1        | 1000    | 289,395     | 0          | 0            | 0.01       | Yes  |
| 1        | 500     | 148,430     | 0          | 0            | 0.01       | Yes  |
| 5        | 3000    | 147,980     | 1,097      | 0.74         | 13.99      | Yes  |
| 1        | 250     | 74,616      | 0          | 0            | 0.01       | Yes  |
| 2        | 3500    | 215,273     | 2,320      | 1.08         | 1.44       | No   |
| 4        | 5000    | 58,279      | 4,467      | 7.66         | 25.82      | No   |
| 3        | 5000    | 27,100      | 4,358      | 16.08        | 26.13      | No   |
| 3        | 4000    | 51,513      | 2,969      | 5.76         | 17.58      | No   |
| 5        | 5000    | 107,554     | 3,181      | 2.96         | 9.40       | No   |
| 5        | 4000    | 21,470      | 3,424      | 15.95        | 25.05      | No   |

**Ceilings with cap=500:** 1 cluster ≥1750; 2 clusters 3000; 3 clusters 3000; 4 clusters 4000; 5 clusters 3000.

---

## 3. Persist batch-size cap (the “CAP” issue)

We introduced **SPACETIMEDB_PERSIST_BATCH_SIZE=500** to send multiple smaller HTTP requests per persist window instead of one large one, aiming to use SpacetimeDB’s throughput better.

**Observed:** With the cap, each cluster sends several sequential requests per persist (e.g. 2500 entities → 5 requests of 500). Total persist time per window often reached **~1–2 seconds**, blocking the tick loop and increasing client errors. Without the cap, a single large request often completed in **~200–800 ms**.

**Comparison (3 clusters):**

| players | With cap (500)     | Without cap (earlier run) |
|--------|---------------------|----------------------------|
| 3000   | PASS (0.02%)        | PASS (0.11%)               |
| 4000   | **FAIL (5.76%)**    | **PASS (0.4%)**            |
| 5000   | FAIL (16.08% / 13.71%) | FAIL (7.25%)           |

So **removing the cap (or setting it to 0) improved results** for this workload; the cap added overhead and worsened error rates. Use **PersistBatchSize=0** for “no cap” in the script.

---

## 4. Arcane + SpacetimeDB with batch cap = 0 (no cap)

Re-run of configurations that **failed with cap=500**, using **`-PersistBatchSize 0`** (single request per persist window).

| clusters | players | total_calls | total_errs | err_rate_pct | lat_avg_ms | pass |
|----------|---------|-------------|------------|--------------|------------|------|
| 3        | 4000    | 1,143,926   | 0          | 0            | 0          | Yes  |
| 3        | 5000    | 381,346     | 121        | 0.03         | 2.2        | Yes  |
| 5        | 4000    | 298,544     | 1,458      | 0.49         | 5.4        | Yes  |
| 4        | 5000    | 61,357      | 3,072      | 5.01         | 26.4       | No   |
| 5        | 5000    | 28,278      | 4,540      | 16.05        | 14.2       | No   |

With **no cap**, 3-cluster 4000 and 5000 and 5-cluster 4000 **pass** (they failed with cap=500). 4-cluster 5000 and 5-cluster 5000 still fail. Recommendation: use **`-PersistBatchSize 0`** for scaling runs unless you have evidence that a smaller batch size helps on your setup.

---

## 5. Ten clusters (no cap) — ceiling sweep

With **10 clusters** and **`-PersistBatchSize 0`**, more clusters reduce load per cluster but increase replication (each cluster has 9 neighbors). Same hardware: load is distributed, replication is multiplied.

| clusters | players | total_calls | total_errs | err_rate_pct | lat_avg_ms | pass |
|----------|---------|-------------|------------|--------------|------------|------|
| 10       | 5000    | 146,568     | 40         | 0.03         | 13.8       | Yes  |
| 10       | 5500    | 58,292      | 552        | 0.95         | 30.8       | Yes  |
| 10       | 5750    | 76,238      | 1,206      | 1.58         | 22.2       | No   |
| 10       | 6000    | 69,446      | 3,074      | 4.43         | 19.8       | No   |

**10-cluster ceiling: 5500 players** (5500 passes at 0.95%; 5750 fails at 1.58%).

---

*Generated from `arcane_scaling_sweep.csv` and `spacetimedb_ceiling_sweep.csv`. Script: `Run-ArcaneScalingSweep.ps1`; use `-PersistBatchSize 0` for no batch cap (recommended).*
