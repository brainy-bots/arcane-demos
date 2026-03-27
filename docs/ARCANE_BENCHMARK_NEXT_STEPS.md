# Arcane benchmark: next steps (observability vs Docker vs Kubernetes)

This doc compares three ways to move from "we have baseline numbers but don't know the bottleneck" to "we can scale and automate confidently." See [ARCANE_BENCHMARK_SETUP.md](ARCANE_BENCHMARK_SETUP.md) for recorded numbers and how to run the ceiling sweep.

---

## Option A: Lightweight observability

**What:** Add minimal instrumentation so that during a run near the ceiling we see *which* component is busiest: e.g. log or sample CPU (or "busy") for swarm, each cluster process, manager, and optionally Redis; or use OS tools (Task Manager, `top`) while holding N at 1100 and C=2 vs C=4.

**Pros:** Fast to add, no new infra, answers "who is at 100% or degrading when we add clusters."  
**Cons:** Manual, single-machine only; no enforced resource limits.

**Best for:** Quick answer to "is it swarm, Redis, or a cluster?" before committing to heavier tooling.

---

## Option B: Docker with fixed resources

**What:** Run each component in a container with **fixed CPU (and optionally memory) limits** (e.g. swarm 2 cores, each cluster 1 core, manager 0.5, Redis 0.5). Ramp N and C until one container hits its limit; that component is the bottleneck. Repeat with relaxed limits to confirm N_max moves.

**Pros:** Controlled, reproducible; isolates the bottleneck without K8s. Good for local and CI.  
**Cons:** No auto-scale, no multi-node out of the box; you manage compose and limits yourself.

**Best for:** Reproducible benchmarks and "which component is the cap" with clear resource boundaries, before investing in orchestration.

---

## Option C: Kubernetes

**What:** Deploy clusters, manager, Redis, and (optionally) the swarm as K8s workloads. Use Deployments/StatefulSets, Services, and optionally HPA or custom controllers to spin clusters up/down. Run benchmarks as Jobs or from outside the cluster.

**Pros:** Easy to spin up/down clusters and servers; natural fit for assigned capabilities (resource requests/limits per pod); simplifies future cloud benchmark runs and automation.  
**Cons:** Non-trivial setup (manifests, networking, storage, RBAC, possibly Helm); we still don't know the bottleneck, so we might tune the wrong thing first.

**Best for:** When we already understand the system (bottleneck, scaling behavior) and want to automate scaling and cloud runs.

---

## Recommendation: visibility first, then decide

- **Right now:** We don't know which component is the limit. Adding Kubernetes before we have that answer adds operational complexity without telling us *what* to scale or *where* to set limits.
- **Suggested order:**
  1. **Add lightweight observability** (Option A): one or two runs at the ceiling with CPU/busy view per process (or a tiny metrics export). Identify the bottleneck.
  2. **Optional:** If you want reproducible, resource-bounded runs, add **Docker with fixed limits** (Option B) and re-run the ceiling sweep. That gives a clear "under these limits, component X is the cap" and a path to CI.
  3. **Then consider Kubernetes** (Option C) when: (a) the bottleneck and scaling behavior are understood, and (b) we want to automate spin-up/down and cloud benchmarks. At that point K8s pays off: we know what to scale (e.g. cluster replicas) and what to limit (e.g. CPU per cluster pod).

**Bottom line:** Kubernetes is justified when we're ready to automate and scale; until then, get visibility (Option A, then optionally B) so we know what we're scaling and why.
