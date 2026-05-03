//! Self-contained clustering simulation binary.
//!
//! No WebSocket, no Redis, no Tokio. Drives DemoAgent group behaviour through
//! ClusterManager and emits one JSON line per evaluation cycle to stdout.
//!
//! Build:
//!   cargo build -p arcane-demo --bin arcane-clustering-sim --features clustering-sim
//!
//! Usage:
//!   arcane-clustering-sim [--agents N] [--group-size N] [--zones N] [--ticks N]
//!                         [--eval-interval N] [--model rules|affinity] [--compare]

use arcane_demo::{run_sim, Model, SimConfig};

fn parse_args() -> (SimConfig, bool) {
    let args: Vec<String> = std::env::args().collect();
    let mut cfg = SimConfig::default();
    let mut compare = false;

    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "--agents" => {
                i += 1;
                cfg.total_agents = args[i].parse().unwrap_or(cfg.total_agents);
            }
            "--group-size" => {
                i += 1;
                cfg.group_size = args[i].parse().unwrap_or(cfg.group_size);
            }
            "--zones" => {
                i += 1;
                cfg.zones = args[i].parse().unwrap_or(cfg.zones);
            }
            "--ticks" => {
                i += 1;
                cfg.ticks = args[i].parse().unwrap_or(cfg.ticks);
            }
            "--eval-interval" => {
                i += 1;
                cfg.eval_interval = args[i].parse().unwrap_or(cfg.eval_interval);
            }
            "--model" => {
                i += 1;
                cfg.model = match args[i].as_str() {
                    #[cfg(feature = "clustering-sim")]
                    "affinity" => Model::Affinity,
                    _ => Model::Rules,
                };
            }
            "--compare" => {
                compare = true;
            }
            _ => {}
        }
        i += 1;
    }
    (cfg, compare)
}

fn main() {
    let (cfg, compare) = parse_args();

    if compare {
        // Run rules first, then affinity, interleaved by tick in the output
        let rules_result = run_sim(&SimConfig {
            model: Model::Rules,
            total_agents: cfg.total_agents,
            group_size: cfg.group_size,
            zones: cfg.zones,
            ticks: cfg.ticks,
            eval_interval: cfg.eval_interval,
        });

        #[cfg(feature = "clustering-sim")]
        let affinity_result = run_sim(&SimConfig {
            model: Model::Affinity,
            total_agents: cfg.total_agents,
            group_size: cfg.group_size,
            zones: cfg.zones,
            ticks: cfg.ticks,
            eval_interval: cfg.eval_interval,
        });

        // Interleave output sorted by tick
        let mut all: Vec<_> = rules_result.snapshots.iter().collect();
        #[cfg(feature = "clustering-sim")]
        all.extend(affinity_result.snapshots.iter());
        all.sort_by_key(|s| (s.tick, s.model));

        for s in all {
            println!(
                "{{\"tick\":{},\"model\":\"{}\",\"clusters\":{},\"group_fragmentation\":{:.4},\"groups_co_located\":{}}}",
                s.tick, s.model, s.cluster_count, s.group_fragmentation, s.groups_co_located
            );
        }

        eprintln!(
            "FINAL rules  group_fragmentation={:.4}",
            rules_result.final_fragmentation
        );
        #[cfg(feature = "clustering-sim")]
        eprintln!(
            "FINAL affinity group_fragmentation={:.4}",
            affinity_result.final_fragmentation
        );
    } else {
        let result = run_sim(&cfg);
        for s in &result.snapshots {
            println!(
                "{{\"tick\":{},\"model\":\"{}\",\"clusters\":{},\"group_fragmentation\":{:.4},\"groups_co_located\":{}}}",
                s.tick, s.model, s.cluster_count, s.group_fragmentation, s.groups_co_located
            );
        }
        eprintln!(
            "FINAL group_fragmentation={:.4}",
            result.final_fragmentation
        );
    }
}
