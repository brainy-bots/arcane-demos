//! Headless player swarm for load-testing SpacetimeDB.
//!
//! Spawns N [`stub_client`]s, each on its own thread, simulating realistic Unreal
//! client behavior (WebSocket connection, subscriptions, reducer calls).
//!
//! # Architecture
//!
//! ```text
//!  ┌─────────────────────────────────────┐
//!  │         arcane-swarm-sdk            │
//!  │  (main binary — CLI + orchestrator) │
//!  └────────────────┬────────────────────┘
//!                   │ spawns N threads
//!       ┌───────────┼───────────────┐
//!       ▼           ▼               ▼
//!  ┌─────────┐ ┌─────────┐    ┌─────────┐
//!  │ Stub    │ │ Stub    │ .. │ Stub    │   ← stub_client.rs
//!  │ Client  │ │ Client  │    │ Client  │      (Unreal replacement)
//!  │ (WS+sub)│ │ (WS+sub)│    │ (WS+sub)│
//!  └────┬────┘ └────┬────┘    └────┬────┘
//!       │           │              │
//!       ▼           ▼              ▼
//!  ┌────────────────────────────────────┐
//!  │  SpacetimeDB  (WebSocket server)   │
//!  └────────────────────────────────────┘
//! ```
//!
//! # Modules
//!
//! - [`metrics`]      — Lock-free atomic counters (calls, latency, subscription events)
//! - [`movement`]     — Deterministic player movement simulation
//! - [`stub_client`]  — One simulated player: connect, subscribe, game loop, disconnect

pub mod metrics;
pub mod movement;
pub mod stub_client;

use metrics::Metrics;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

const DEFAULT_HOST: &str = "http://127.0.0.1:3000";
const DEFAULT_DB: &str = "arcane";

/// Seconds at the start of the test where metrics are collected but not
/// included in the final summary. Lets connections stabilize.
const WARMUP_SECS: u64 = 5;

/// CLI configuration parsed from command-line arguments.
pub struct SwarmConfig {
    pub players: u32,
    pub tick_rate: u32,
    pub duration_secs: u64,
    pub clustered: bool,
    pub actions_per_sec: f64,
    pub host: String,
    pub db_name: String,
    /// If true, stub sends input (update_player_input) and server runs physics; else sends position (update_player).
    pub server_physics: bool,
}

impl Default for SwarmConfig {
    fn default() -> Self {
        Self {
            players: 100,
            tick_rate: 10,
            duration_secs: 30,
            clustered: false,
            actions_per_sec: 2.0,
            host: DEFAULT_HOST.to_string(),
            db_name: DEFAULT_DB.to_string(),
            server_physics: false,
        }
    }
}

/// Parse CLI args into a [`SwarmConfig`].
pub fn parse_args() -> SwarmConfig {
    let args: Vec<String> = std::env::args().collect();
    let mut cfg = SwarmConfig::default();
    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "--players" | "-n" => { i += 1; cfg.players = args[i].parse().unwrap(); }
            "--tick-rate"      => { i += 1; cfg.tick_rate = args[i].parse().unwrap(); }
            "--duration" | "-d"=> { i += 1; cfg.duration_secs = args[i].parse().unwrap(); }
            "--mode" | "-m"    => { i += 1; cfg.clustered = args[i] == "clustered"; }
            "--aps"            => { i += 1; cfg.actions_per_sec = args[i].parse().unwrap(); }
            "--host"           => { i += 1; cfg.host = args[i].clone(); }
            "--db"             => { i += 1; cfg.db_name = args[i].clone(); }
            "--server-physics" => { cfg.server_physics = true; }
            _ => {}
        }
        i += 1;
    }
    cfg
}

/// Entry point: parse CLI, spawn players, report metrics, shut down.
pub fn run() {
    let cfg = parse_args();
    let tick_interval = Duration::from_micros(1_000_000 / cfg.tick_rate as u64);
    let mode_name = if cfg.clustered { "clustered" } else { "spread" };

    eprintln!(
        "arcane-swarm-sdk: {} players, {} Hz, mode={}, aps={:.1}, duration={}s, server_physics={}",
        cfg.players, cfg.tick_rate, mode_name, cfg.actions_per_sec, cfg.duration_secs, cfg.server_physics,
    );
    eprintln!("  SpacetimeDB: {}/database/{}", cfg.host, cfg.db_name);
    eprintln!("  Each player = 1 WebSocket + BSATN + subscription (identical to Unreal SDK)");

    let metrics = Arc::new(Metrics::new());
    let stop = Arc::new(AtomicBool::new(false));
    let connected = Arc::new(AtomicU64::new(0));

    let handles = spawn_players(&cfg, tick_interval, &metrics, &stop, &connected);
    wait_all_connected(&connected, cfg.players);

    let reporter = spawn_reporter(cfg.players, &metrics, &stop);

    std::thread::sleep(Duration::from_secs(cfg.duration_secs));
    eprintln!("\narcane-swarm-sdk: duration reached, shutting down...");
    stop.store(true, Ordering::Relaxed);

    for h in handles {
        let _ = h.join();
    }
    let _ = reporter.join();
    eprintln!("arcane-swarm-sdk: done.");
}

// ── Internal helpers ─────────────────────────────────────────────────────

fn spawn_players(
    cfg: &SwarmConfig,
    tick_interval: Duration,
    metrics: &Arc<Metrics>,
    stop: &Arc<AtomicBool>,
    connected: &Arc<AtomicU64>,
) -> Vec<std::thread::JoinHandle<()>> {
    let mut handles = Vec::with_capacity(cfg.players as usize);

    for idx in 0..cfg.players {
        let client_cfg = stub_client::StubClientConfig {
            idx,
            total_players: cfg.players,
            clustered: cfg.clustered,
            host: cfg.host.clone(),
            db_name: cfg.db_name.clone(),
            tick_interval,
            actions_per_sec: cfg.actions_per_sec,
            server_physics: cfg.server_physics,
        };
        let m = metrics.clone();
        let s = stop.clone();
        let cc = connected.clone();

        let h = std::thread::Builder::new()
            .name(format!("player-{}", idx))
            .stack_size(512 * 1024)
            .spawn(move || stub_client::run(client_cfg, m, s, cc))
            .expect("spawn player thread");

        handles.push(h);

        if idx % 50 == 49 {
            std::thread::sleep(Duration::from_millis(500));
            eprintln!("  Spawned {} player threads...", idx + 1);
        }
    }

    handles
}

fn wait_all_connected(connected: &Arc<AtomicU64>, total: u32) {
    let start = Instant::now();
    let timeout = Duration::from_secs(30);
    loop {
        let c = connected.load(Ordering::Relaxed);
        if c >= total as u64 {
            eprintln!("  All {} players connected and subscribed.", total);
            break;
        }
        if start.elapsed() > timeout {
            eprintln!(
                "  WARNING: Only {}/{} connected after {}s, continuing...",
                c, total, timeout.as_secs()
            );
            break;
        }
        std::thread::sleep(Duration::from_millis(200));
    }
}

/// Heuristic: which component is likely failing (for visibility when debugging ceiling).
fn bottleneck_hint(players: u32, snap: &metrics::Snapshot) -> &'static str {
    let total = snap.calls + snap.errs;
    let err_rate = if total > 0 { snap.errs as f64 / total as f64 } else { 0.0 };
    let expected_sub_per_sec = (players as u64).saturating_mul(players as u64).saturating_mul(10);
    let sub_ok = expected_sub_per_sec == 0 || snap.sub_total() >= expected_sub_per_sec / 4;
    if err_rate >= 0.01 {
        "hint: REDUCER_STRESS (high err)"
    } else if snap.p99_latency_ms() >= 200.0 {
        "hint: REDUCER_LATENCY (p99 high)"
    } else if !sub_ok {
        "hint: SUB_BACKLOG (low sub_rx)"
    } else {
        "hint: OK"
    }
}

fn spawn_reporter(
    players: u32,
    metrics: &Arc<Metrics>,
    stop: &Arc<AtomicBool>,
) -> std::thread::JoinHandle<()> {
    let m = metrics.clone();
    let s = stop.clone();

    std::thread::spawn(move || {
        let mut elapsed = 0u64;
        let mut total_calls: u64 = 0;
        let mut total_oks: u64 = 0;
        let mut total_errs: u64 = 0;
        let mut total_latency_sum_us: u64 = 0;
        let mut total_latency_samples: u64 = 0;
        loop {
            std::thread::sleep(Duration::from_secs(1));
            elapsed += 1;
            let snap = m.snapshot_and_reset();
            total_calls += snap.calls + snap.errs;
            total_oks += snap.oks;
            total_errs += snap.errs;
            total_latency_sum_us += snap.avg_latency_us * snap.oks; // approx sum for this second
            total_latency_samples += snap.oks;
            let warmup_tag = if elapsed <= WARMUP_SECS { " (warmup)" } else { "" };
            let hint = bottleneck_hint(players, &snap);
            eprintln!(
                "[{:4}s]{} players={} calls={} ok={} err={} lat avg={:.1}ms p50={:.1}ms p95={:.1}ms p99={:.1}ms max={:.1}ms | sub_rx={} (upd={} ins={} del={}) | {}",
                elapsed, warmup_tag, players,
                snap.calls, snap.oks, snap.errs,
                snap.avg_latency_ms(), snap.p50_latency_ms(),
                snap.p95_latency_ms(), snap.p99_latency_ms(),
                snap.max_latency_ms(),
                snap.sub_total(), snap.sub_updates, snap.sub_inserts, snap.sub_deletes,
                hint,
            );
            if s.load(Ordering::Relaxed) {
                let lat_avg_ms = if total_latency_samples > 0 {
                    total_latency_sum_us as f64 / 1000.0 / total_latency_samples as f64
                } else {
                    0.0
                };
                eprintln!(
                    "FINAL: players={} total_calls={} total_oks={} total_errs={} lat_avg_ms={:.2}",
                    players, total_calls, total_oks, total_errs, lat_avg_ms,
                );
                break;
            }
        }
    })
}
