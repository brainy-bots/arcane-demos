//! Cluster server binary with demo agents (gravity, jump, wander).
//! Demo-only: use this for Unreal/replication demos. For infrastructure-only cluster, use arcane-infra's arcane-cluster.
//!
//! Env: same as arcane-cluster, plus:
//!   DEMO_ENTITIES   — number of demo entities (e.g. 25).
//!
//! Example:
//!   CLUSTER_ID=... DEMO_ENTITIES=25 cargo run -p arcane-demo --bin arcane-cluster-demo

use std::env;

use arcane_demo::{agents_to_entries, create_demo_agents, tick_demo_agents};
use arcane_infra::cluster_runner;
use uuid::Uuid;

fn parse_uuids(s: &str) -> Vec<Uuid> {
    s.split(',')
        .map(|x| x.trim())
        .filter(|x| !x.is_empty())
        .filter_map(|x| Uuid::parse_str(x).ok())
        .collect()
}

fn main() -> Result<(), String> {
    let cluster_id =
        env::var("CLUSTER_ID").map_err(|_| "CLUSTER_ID env var required (UUID)".to_string())?;
    let cluster_id =
        Uuid::parse_str(&cluster_id).map_err(|e| format!("invalid CLUSTER_ID: {}", e))?;

    let redis_url = env::var("REDIS_URL").unwrap_or_else(|_| "redis://127.0.0.1:6379".to_string());
    let neighbor_ids = env::var("NEIGHBOR_IDS")
        .map(|s| parse_uuids(&s))
        .unwrap_or_default();

    let ws_port: u16 = env::var("CLUSTER_WS_PORT")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(8080);

    let n_demo = env::var("DEMO_ENTITIES")
        .ok()
        .and_then(|s| s.parse::<u32>().ok())
        .unwrap_or(0)
        .min(2000);
    let stress_radius = env::var("STRESS_RADIUS")
        .ok()
        .and_then(|s| s.parse::<f64>().ok())
        .filter(|&r| r > 0.0);
    if let Some(r) = stress_radius {
        eprintln!(
            "stress mode: entities confined to radius {} around center (same-place)",
            r
        );
    }
    let mut demo_agents = if n_demo > 0 {
        eprintln!("demo agents: {} (gravity, jump, wander)", n_demo);
        create_demo_agents(n_demo, cluster_id, stress_radius)
    } else {
        vec![]
    };

    cluster_runner::run_cluster_loop(
        cluster_id,
        redis_url,
        neighbor_ids,
        ws_port,
        move |tick_count| {
            tick_demo_agents(&mut demo_agents, tick_count, stress_radius);
            agents_to_entries(&demo_agents, cluster_id)
        },
        None, // no ClusterSimulation
    )
}
