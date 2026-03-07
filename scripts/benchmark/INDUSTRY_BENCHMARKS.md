# Industry benchmarks and how to show this library is better than other options

**Reality:** There is **no single industry-standard benchmark** (like SPEC for CPUs) for game networking or replication. Reputation comes from **cited scenarios**, **vendor/community benchmarks**, and **comparable metrics**. To show this library is better than other options in the market, we should **align with the scenarios and metrics the industry already uses** and, where possible, **run or cite the same tests** others run.

---

## What has reputation today

### 1. Unity: open comparison benchmark

- **StinkySteak/unity-netcode-benchmark** (GitHub): Compares **Fusion, Fishnet, Mirror, Mirage, Netick, NGO** (Netcode for GameObjects) on the same test. People use it to compare “other options in the market.”
- **We are Unreal**, so we cannot run this binary. We can still use the **same kind of metrics**: client FPS, server tick time, bandwidth, max player/entity count at a target frame rate.

### 2. Photon (vendor)

- **Photon Fusion**: Published numbers — **200 players @ 60 Hz**, **~6× less bandwidth** than MLAPI/Mirror, zero runtime allocations. Often cited as “the bar” for high-player-count networking.
- **Takeaway:** Reporting **“N entities at 60 Hz (or 30 Hz) with X KB/s bandwidth”** puts us in the same conversation. We need **bandwidth** in our benchmark to be comparable.

### 3. Unreal / Epic

- **Default replication:** Widely known to struggle at scale (e.g. “100 players in one place” → **~10–15 FPS** on server, **~66 ms NetBroadcastTickTime**). This is the **baseline** you mentioned; most shipped games use custom or optimized layers on top.
- **Iris:** Epic’s modern stack (Fortnite-style). Cited numbers: **100 players**, **~61% less bandwidth** than default, **~21% less server CPU**. “100 players in one place” is a **recognized stress scenario** (e.g. BorMor blog, Epic docs).
- **Replication Graph:** 100 clients, **50k+ replicated actors** (Fortnite). Same idea: high entity count, spatial optimization.

### 4. Improbable / SpatialOS

- **Scale (updated):** **1,000-player** mode (Mavericks: Proving Grounds), **4,000+** in one world (Scavengers public event), **5,000** in shared environment (ScavLab), and a **14,000-player stress test** (same spot) in Scavengers. So “thousands in the same place” is the bar they set; 1k is a cited milestone.
- **200-player FPS** starter project (GDK demo). Used as “we can run X players” proof points, not a standard test suite.

### 5. Others (Nakama, custom engines)

- Backend/CCU load tests (e.g. 2M concurrent users) — different problem (lobby/matchmaking vs. in-world replication). Useful for “scale” story but not the same benchmark as “replication at 60 Hz.”

---

## How to show this library is better than other options

We cannot run Photon’s or Epic’s binary. We **can**:

1. **Use the same scenarios and metrics** they use, so our numbers are **directly comparable**.
2. **Add the metrics that are cited** (bandwidth, server tick time, entity count at target FPS).
3. **Optionally** provide an **open Unreal benchmark** (same scenario, multiple backends) so others can run it and compare.

### Recommended alignment

| Scenario / metric        | Who uses it        | What we do |
|-------------------------|--------------------|------------|
| **“100 entities in one place”** | BorMor, Epic (Iris), default UE stress | Run our demo with **100 entities**, same “everyone visible” setup. Report **client FPS**, **server tick_ms** (we have this), and **bandwidth** if we can measure it. Then we can say: “Same scenario as BorMor/Iris: default UE ~10–15 FPS server; we get X.” |
| **Max entities at 30/60 FPS**   | Photon (200 @ 60 Hz), Epic (100)       | Report **max N** such that client FPS ≥ 30 (or 60) and server tick stays below a threshold. Publish as “Arcane: N entities at 60 Hz” next to “Photon: 200 players @ 60 Hz” and “Epic/Iris: 100 players.” |
| **Bandwidth (KB/s)**            | Photon, Epic (61% reduction)           | Add **client receive** and/or **server send** rate to our benchmark so we can say “X KB/s at N entities” and compare to vendor numbers. |
| **Server load (tick time)**     | BorMor (66 ms default), Epic           | We already log **tick_ms**. Report it in the same way (e.g. “server tick &lt; 5 ms at 200 entities”). |

### Concrete next steps

1. **Document our scenario** so it’s comparable:  
   “Same as ‘100 players in one place’ stress: one map, N entities, all visible, third-person character. We report client FPS (median), server tick_ms (avg), and entity count.”

2. **Add bandwidth** to the benchmark (e.g. Unreal client net stats or our own WebSocket/server counters). Then the CSV and report include **bandwidth** and we can compare to Photon/Epic.

3. **Publish a one-page “benchmark report”** (PDF or Markdown in repo):  
   Scenario, hardware, methodology, and results table: **Default Unreal** vs **Iris** (if we can run it) vs **Arcane** — same map, same N, same metrics. That gives a single place that says “this library is better than these options” in the same way the Unity netcode benchmark does for Unity.

4. **Optional:** Turn our benchmark into an **open Unreal replication benchmark** (e.g. “Unreal Replication Benchmark”: default UE, Iris, Arcane, same scenario and metrics). Others could add more backends. That would build reputation over time as the “industry” benchmark for Unreal, even if it’s community-driven.

---

## Vendor demo projects: can we use them?

### Available online

| Vendor      | Project | Engine | License | URL |
|------------|---------|--------|---------|-----|
| **SpatialOS** | GDK for Unreal Example Project | Unreal | **MIT** | github.com/spatialos/UnrealGDKExampleProject |
| **SpatialOS** | GDK for Unreal (plugin) | Unreal | **MIT** | github.com/spatialos/UnrealGDK |
| **SpatialOS** | FPS Starter Project | Unity | **MIT** | github.com/spatialos/gdk-for-unity-fps-starter-project (archived) |
| **Photon** | Various Fusion samples | Unity | Per-repo (e.g. MIT for some) | Community repos (Fusion-FPS, Hathora samples, etc.); Photon SDK itself is commercial |

So **yes**, vendor demo/sample projects are available. The **SpatialOS Unreal example** and **GDK plugin** are on GitHub and **MIT-licensed**.

### Is it legal to grab and modify to use our networking?

- **SpatialOS (MIT):** **Yes.** MIT allows use, modification, and distribution. You can take their Unreal example project, replace or bypass their GDK/networking layer with our adapter, and ship or benchmark the result. You must **retain the MIT license and copyright notice** for their code you keep; new code (our networking) is yours.
- **Photon:** Their **SDK** is commercial (subscription). Sample projects that depend on Photon typically require a Photon account to run. You could build a **new** project that mirrors their demo’s scenario (map, player count, metrics) and use our networking instead—that’s “our own” demo, not forking their code. No legal issue with that.

So: **using the SpatialOS Unreal example and swapping in our networking is legal under MIT**, as long as we comply with MIT (retain notice, state changes). Creating our own demo that merely mirrors a vendor’s scenario (e.g. “1000 entities, same place”) is always fine.

### Best way to show our networking works and is powerful

**Option A — Use a vendor demo (SpatialOS Unreal):**

- Clone **UnrealGDKExampleProject** (and the **UnrealGDK** plugin).
- Understand how they spawn/replicate entities and send state.
- Add our client plugin (or a minimal adapter) and **switch** the game to use our backend instead of SpatialOS (same map, same scenario).
- Run the same scenario (e.g. N entities in one place), record FPS, server tick, bandwidth.
- **Pros:** Same game, same scenario; direct “apples-to-apples” comparison (SpatialOS vs Arcane on the same project). **Cons:** We depend on their project structure and must keep their license for their assets/code we keep.

**Option B — Create our own benchmark demo:**

- Keep (or extend) our current Unreal demo so it **mirrors the cited scenario**: e.g. “1000 entities in one place,” same metrics (FPS, tick_ms, bandwidth).
- Publish methodology and results. No vendor code dependency.
- **Pros:** Full control, no GDK dependency, clear “our stack, our numbers.” **Cons:** Not the same binary as a vendor’s; we claim “same scenario” rather than “same project.”

**Recommendation:**

- If the goal is **“same game, our networking”** and a direct comparison to SpatialOS-style integration: **Option A** (take their Unreal example, MIT, swap networking). It’s legal and gives the strongest “apples-to-apples” story.
- If the goal is **simplicity and full control** (no dependency on their GDK or build): **Option B** (our own demo, same scenario and metrics). We already have an Unreal demo; we scale it to the same scenario (e.g. 1000 entities, “in one place”) and document it as the benchmark.

Either way, **document the scenario and metrics** (entity count, FPS, server tick, bandwidth) so results are comparable to SpatialOS (1k–14k), Photon (200 @ 60 Hz), and Epic (100 / Iris).

---

## Summary

- **Industry-wide benchmarks with reputation:** Unity has an open netcode benchmark (StinkySteak); Unreal does **not** have an equivalent standard. Vendors (Photon, Epic, Improbable) publish their own numbers and scenarios. **SpatialOS** has demonstrated **1k–14k players** (Mavericks 1k, Scavengers 4k+ / 5k / 14k stress).
- **Vendor demos:** **SpatialOS Unreal example** (UnrealGDKExampleProject, UnrealGDK) is **on GitHub and MIT-licensed**. We can grab it, replace their networking with ours, and run the same scenario—**legally**—as long as we retain the MIT notice for their code.
- **Best way to show our networking works:** (1) **Use their demo:** Take the SpatialOS Unreal example, swap in our networking, same scenario → direct comparison. (2) **Or create our own:** Our Unreal demo, same scenario (e.g. 1000 entities in one place) and metrics → full control, no vendor dependency. In both cases, report the same metrics (FPS, server tick, bandwidth) so we’re comparable to SpatialOS, Photon, and Epic.

**See also:** **`SCENARIO_AND_CRITICISMS.md`** — defines the **SpatialOS-style scenario parameters** we match in our own demo (entity count, “same place,” tick rate, map, metrics) and lists **industry criticisms** of SpatialOS with how we address them (local iteration, no lock-in, no version churn, no cloud dependency, no ToS risk, simpler model). For a **SpacetimeDB-only** networking version to measure SpacetimeDB's limit and compare to Arcane, see **`docs/SPACETIMEDB_NETWORKING_PLAN.md`**.
