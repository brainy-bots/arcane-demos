//! Node server binary with demo agents (gravity, jump, wander).
//! Demo-only: use this for Unreal/replication demos. For infrastructure-only node, use arcane-infra's arcane-node.
//!
//! Env: same as arcane-node, plus:
//!   DEMO_ENTITIES   — number of demo entities (e.g. 25).
//!
//! Example:
//!   NODE_ID=... DEMO_ENTITIES=25 cargo run -p arcane-demo --bin arcane-node-demo

use std::env;

use arcane_demo::{
    agents_to_entries, create_demo_agents, projectiles_to_entries, spawn_projectile,
    tick_demo_agents, tick_projectiles, Projectile,
};
use arcane_infra::node_runner;
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
        env::var("NODE_ID").map_err(|_| "NODE_ID env var required (UUID)".to_string())?;
    let cluster_id = Uuid::parse_str(&cluster_id).map_err(|e| format!("invalid NODE_ID: {}", e))?;

    let redis_url = env::var("REDIS_URL").unwrap_or_else(|_| "redis://127.0.0.1:6379".to_string());
    let neighbor_ids = env::var("NEIGHBOR_IDS")
        .map(|s| parse_uuids(&s))
        .unwrap_or_default();

    let ws_port: u16 = env::var("NODE_WS_PORT")
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

    let mut projectiles: Vec<Projectile> = Vec::new();

    node_runner::run_node_loop(
        cluster_id,
        redis_url,
        neighbor_ids,
        ws_port,
        move |tick_count| {
            tick_demo_agents(&mut demo_agents, tick_count, stress_radius);

            // Generate demo fire actions: every agent shoots occasionally toward center
            // (For testing collision/explosion; real gameplay gets actions from clients)
            if tick_count % 60 == 0 && !demo_agents.is_empty() {
                for agent in demo_agents.iter() {
                    let center_x = 2500.0;
                    let center_z = 2500.0;
                    let dx = center_x - agent.x;
                    let dz = center_z - agent.z;
                    let dy = 50.0; // aim slightly upward
                    projectiles.push(spawn_projectile(
                        (agent.x, agent.y, agent.z),
                        agent.entity_id,
                        (dx, dy, dz),
                    ));
                }
            }

            tick_projectiles(&mut projectiles, tick_count, stress_radius);

            // Detect collisions and apply explosions
            let collisions = {
                let mut to_remove = Vec::new();
                let mut explosions = Vec::new();

                for (p_idx, proj) in projectiles.iter().enumerate() {
                    let mut hit_agents = Vec::new();
                    for agent in demo_agents.iter() {
                        if agent.entity_id != proj.owner_id {
                            let dx = proj.x - agent.x;
                            let dy = proj.y - agent.y;
                            let dz = proj.z - agent.z;
                            let dist_sq = dx * dx + dy * dy + dz * dz;
                            let collision_dist = 40.0; // projectile + agent radius
                            if dist_sq < collision_dist * collision_dist {
                                hit_agents.push(agent.entity_id);
                            }
                        }
                    }
                    if !hit_agents.is_empty() {
                        explosions.push((proj.clone(), hit_agents));
                        to_remove.push(p_idx);
                    }
                }

                for idx in to_remove.iter().rev() {
                    projectiles.remove(*idx);
                }

                explosions
            };

            // Apply explosion impulses: radial push to agents in radius
            for (proj, _hit_agents) in collisions {
                const EXPLOSION_RADIUS: f64 = 150.0;
                const EXPLOSION_IMPULSE: f64 = 500.0;
                const TICK_DT: f64 = 1.0 / 20.0; // 20 Hz

                for agent in demo_agents.iter_mut() {
                    let dx = agent.x - proj.x;
                    let dz = agent.z - proj.z;
                    let dist_sq = dx * dx + dz * dz;

                    if dist_sq < EXPLOSION_RADIUS * EXPLOSION_RADIUS && dist_sq > 0.0001 {
                        let dist = dist_sq.sqrt();
                        let nx = dx / dist;
                        let nz = dz / dist;

                        let falloff = 1.0 - (dist / EXPLOSION_RADIUS).min(1.0);
                        let impulse = EXPLOSION_IMPULSE * falloff;
                        agent.vx += nx * impulse * TICK_DT;
                        agent.vz += nz * impulse * TICK_DT;
                    }
                }
            }

            let mut entries = agents_to_entries(&demo_agents, cluster_id);
            entries.extend(projectiles_to_entries(&projectiles, cluster_id));
            entries
        },
        None,
    )
}
