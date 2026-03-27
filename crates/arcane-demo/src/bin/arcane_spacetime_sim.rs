//! External simulator for SpacetimeDB demo: runs same demo agents as arcane-cluster-demo at 20 Hz,
//! POSTs entity state to SpacetimeDB set_entities reducer each tick.
//! Build with: cargo build -p arcane-demo --bin arcane-spacetime-sim --features spacetime-sim
//!
//! Env: SPACETIMEDB_URI (default http://localhost:3000), DATABASE_NAME (default arcane_demo),
//!      DEMO_ENTITIES, optional STRESS_RADIUS.

use std::time::{Duration, Instant};

use arcane_demo::{create_demo_agents, tick_demo_agents};
use uuid::Uuid;

const TICK_INTERVAL: Duration = Duration::from_millis(50); // 20 Hz

fn main() -> Result<(), String> {
    let uri = std::env::var("SPACETIMEDB_URI").unwrap_or_else(|_| "http://localhost:3000".to_string());
    // Must match the database name we published locally via `spacetime publish arcane --yes`
    let database = std::env::var("DATABASE_NAME").unwrap_or_else(|_| "arcane".to_string());
    let n_entities: u32 = std::env::var("DEMO_ENTITIES")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(200)
        .min(2000);
    let stress_radius: Option<f64> = std::env::var("STRESS_RADIUS")
        .ok()
        .and_then(|s| s.parse().ok())
        .filter(|&r: &f64| r > 0.0);

    let cluster_id = Uuid::new_v4();
    let mut agents = create_demo_agents(n_entities, cluster_id, stress_radius);
    eprintln!(
        "arcane-spacetime-sim: {} entities -> {}/v1/database/{}/call/set_entities (20 Hz)",
        n_entities,
        uri.trim_end_matches('/'),
        database
    );

    let url = format!(
        "{}/v1/database/{}/call/set_entities",
        uri.trim_end_matches('/'),
        database
    );
    let client = reqwest::blocking::Client::builder()
        .timeout(Duration::from_secs(5))
        .build()
        .map_err(|e| e.to_string())?;

    let mut tick_count: u64 = 0;
    loop {
        let start = Instant::now();
        tick_demo_agents(&mut agents, tick_count, stress_radius);
        tick_count = tick_count.wrapping_add(1);

        let entities: Vec<serde_json::Value> = agents
            .iter()
            .map(|a| {
                serde_json::json!({
                    "entity_id": a.entity_id.to_string(),
                    "x": a.x, "y": a.y, "z": a.z,
                    "vx": a.vx, "vy": a.vy, "vz": a.vz,
                })
            })
            .collect();
        let body = serde_json::json!([entities]);

        if let Err(e) = client.post(&url).json(&body).send() {
            eprintln!("set_entities POST error: {}", e);
        }

        let elapsed = start.elapsed();
        if elapsed < TICK_INTERVAL {
            std::thread::sleep(TICK_INTERVAL - elapsed);
        }
    }
}
