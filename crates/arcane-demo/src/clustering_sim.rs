//! Self-contained clustering simulation loop.
//!
//! No WebSocket, no Redis, no async runtime. Drives DemoAgent group behaviour through
//! the clustering model and produces per-evaluation-tick metrics.
//!
//! `run_sim` is the library entry point — callable from integration tests and the
//! `arcane-clustering-sim` binary without spawning a subprocess.
//!
//! ## How it works
//!
//! Each evaluation tick, `run_sim` builds a `WorldStateView` from current agent
//! positions and party assignments, then calls `model.compute_entity_assignments()`
//! to get the model's desired entity→cluster mapping. Fragmentation is computed from
//! that desired mapping — measuring what the model *wants*, not what has executed.
//!
//! This approach is correct for comparing models: AffinityEngine receives party signals
//! and builds an interaction graph over time, so it wants party members co-located.
//! RulesEngine has no party concept — its assignments are purely spatial.

use crate::{compute_group_states, create_grouped_agents, tick_grouped_agents, EncounterTracker};
use arcane_core::{
    clustering_model::{ClusterInfo, PlayerInfo, WorldStateView},
    types::Vec2,
    IClusteringModel,
};
use std::collections::HashMap;
use std::sync::Arc;
use uuid::Uuid;

#[cfg(feature = "clustering-sim")]
use arcane_affinity::AffinityEngine;

/// Which clustering model to evaluate.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Model {
    Rules,
    #[cfg(feature = "clustering-sim")]
    Affinity,
}

/// Configuration for a single simulation run.
pub struct SimConfig {
    pub total_agents: u32,
    pub group_size: u32,
    /// Number of initial cluster zones. Agents are spread evenly across zones at spawn.
    pub zones: u32,
    pub ticks: u64,
    /// Call model evaluation every this many ticks.
    pub eval_interval: u64,
    pub model: Model,
}

impl Default for SimConfig {
    fn default() -> Self {
        Self {
            total_agents: 30,
            group_size: 10,
            zones: 3,
            ticks: 300,
            eval_interval: 10,
            model: Model::Rules,
        }
    }
}

/// Metrics snapshot emitted after each evaluation cycle.
#[derive(Clone, Debug)]
pub struct EvalSnapshot {
    pub tick: u64,
    pub model: &'static str,
    /// Number of distinct desired-assignment clusters in this tick.
    pub cluster_count: usize,
    /// Fraction of groups whose members are desired to be in different clusters (0 = fully co-located).
    pub group_fragmentation: f64,
    /// Number of groups where all members are desired in the same cluster.
    pub groups_co_located: u32,
}

/// Result of a complete simulation run.
pub struct SimResult {
    pub snapshots: Vec<EvalSnapshot>,
    /// group_fragmentation in the last snapshot (1.0 if no evaluations ran).
    pub final_fragmentation: f64,
}

/// Run the clustering simulation. Returns per-evaluation snapshots and the final fragmentation.
pub fn run_sim(cfg: &SimConfig) -> SimResult {
    let model_name: &'static str = match cfg.model {
        Model::Rules => "rules",
        #[cfg(feature = "clustering-sim")]
        Model::Affinity => "affinity",
    };

    let model: Arc<dyn IClusteringModel> = match cfg.model {
        Model::Rules => Arc::new(arcane_rules::RulesEngine::new()),
        #[cfg(feature = "clustering-sim")]
        Model::Affinity => Arc::new(AffinityEngine::default()),
    };

    let mut agents = create_grouped_agents(cfg.total_agents, cfg.group_size, None);

    // Assign initial cluster_ids round-robin by individual agent index so that each
    // group's members are split across all zones from the start. This creates high
    // initial fragmentation that AffinityEngine (with party signals) should resolve
    // while RulesEngine (no party concept) leaves fragmented.
    let zones = cfg.zones.max(1) as usize;
    let zone_ids: Vec<Uuid> = (0..zones)
        .map(|i| Uuid::from_u128(0x1000 + i as u128))
        .collect();
    let initial_cluster: HashMap<Uuid, Uuid> = agents
        .iter()
        .enumerate()
        .map(|(i, a)| (a.entity_id, zone_ids[i % zones]))
        .collect();

    // party_id: entity_id → group_id (for PlayerInfo)
    let party_map: HashMap<Uuid, Uuid> = agents
        .iter()
        .filter_map(|a| a.group_id.map(|g| (a.entity_id, g)))
        .collect();

    let mut tracker = EncounterTracker::new();
    // desired assignments from the previous evaluation — used as current_cluster for scoring
    let mut current_assignments: HashMap<Uuid, Uuid> = initial_cluster.clone();
    let mut snapshots: Vec<EvalSnapshot> = Vec::new();

    for tick in 0..cfg.ticks {
        let group_states = compute_group_states(&agents, &tracker);
        tick_grouped_agents(&mut agents, tick, &group_states, &mut tracker, None);
        tracker.tick();

        if tick > 0 && tick.is_multiple_of(cfg.eval_interval) {
            let view = build_world_view(&agents, &current_assignments, &party_map);
            let desired = model.compute_entity_assignments(&view);

            // Merge: desired assignments override current; unmentioned entities keep current cluster
            for (eid, cid) in &desired {
                current_assignments.insert(*eid, *cid);
            }

            let cluster_count = current_assignments
                .values()
                .collect::<std::collections::HashSet<_>>()
                .len();
            let (frag, co_located) = compute_fragmentation(&agents, &current_assignments);

            snapshots.push(EvalSnapshot {
                tick,
                model: model_name,
                cluster_count,
                group_fragmentation: frag,
                groups_co_located: co_located,
            });
        }
    }

    let final_fragmentation = snapshots.last().map_or(1.0, |s| s.group_fragmentation);
    SimResult {
        snapshots,
        final_fragmentation,
    }
}

fn build_world_view(
    agents: &[crate::DemoAgent],
    current_assignments: &HashMap<Uuid, Uuid>,
    party_map: &HashMap<Uuid, Uuid>,
) -> WorldStateView {
    // Build ClusterInfo from current assignments
    let mut cluster_players: HashMap<Uuid, Vec<Uuid>> = HashMap::new();
    for (eid, cid) in current_assignments {
        cluster_players.entry(*cid).or_default().push(*eid);
    }
    // Build centroids from agent positions
    let agent_pos: HashMap<Uuid, (f64, f64)> =
        agents.iter().map(|a| (a.entity_id, (a.x, a.z))).collect();

    let clusters: Vec<ClusterInfo> = cluster_players
        .iter()
        .map(|(cid, pids)| {
            let n = pids.len().max(1) as f64;
            let (sx, sz) = pids.iter().fold((0.0, 0.0), |(ax, az), pid| {
                let (px, pz) = agent_pos.get(pid).copied().unwrap_or((0.0, 0.0));
                (ax + px, az + pz)
            });
            ClusterInfo {
                cluster_id: *cid,
                server_host: "sim".to_string(),
                player_ids: pids.clone(),
                player_count: pids.len() as u32,
                cpu_pct: 0.0,
                centroid: Vec2::new(sx / n, sz / n),
                spread_radius: 0.0,
                rpc_rate_out: 0.0,
            }
        })
        .collect();

    let players: Vec<PlayerInfo> = agents
        .iter()
        .map(|a| {
            let &cluster_id = current_assignments
                .get(&a.entity_id)
                .unwrap_or(&Uuid::nil());
            PlayerInfo {
                player_id: a.entity_id,
                cluster_id,
                position: Vec2::new(a.x, a.z),
                velocity: Vec2::new(a.vx, a.vz),
                guild_id: None,
                party_id: party_map.get(&a.entity_id).copied(),
            }
        })
        .collect();

    WorldStateView {
        timestamp: 0.0,
        evaluation_budget_ms: 50,
        clusters,
        players,
    }
}

fn compute_fragmentation(
    agents: &[crate::DemoAgent],
    entity_cluster: &HashMap<Uuid, Uuid>,
) -> (f64, u32) {
    let mut group_clusters: HashMap<Uuid, std::collections::HashSet<Uuid>> = HashMap::new();
    for a in agents {
        if let Some(gid) = a.group_id {
            if let Some(&cid) = entity_cluster.get(&a.entity_id) {
                group_clusters.entry(gid).or_default().insert(cid);
            }
        }
    }
    if group_clusters.is_empty() {
        return (0.0, 0);
    }
    let total = group_clusters.len();
    let fragmented = group_clusters.values().filter(|s| s.len() > 1).count();
    let co_located = (total - fragmented) as u32;
    (fragmented as f64 / total as f64, co_located)
}
