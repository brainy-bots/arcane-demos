//! **Unreal Stub Client** — a lightweight stand-in for a real Unreal game client.
//!
//! From SpacetimeDB's perspective this behaves identically to a real Unreal client:
//!
//! - One persistent **WebSocket** connection per player (`DbConnection`)
//! - Binary serialization (**BSATN**), not JSON/HTTP
//! - **Subscription-based reads** — the server pushes row deltas, we never poll
//! - **Reducer calls** for writes — `update_player`, `pickup_item`, `use_item`, `player_interact`
//!
//! The only difference from a real Unreal client is that we skip rendering and
//! use a simple movement model instead of a full character controller.

use crate::module_bindings::*;
use spacetimedb_sdk::{DbContext, Table, TableWithPrimaryKey};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

use super::metrics::Metrics;
use super::movement::PlayerMovement;

/// Configuration for a single stub client.
pub struct StubClientConfig {
    pub idx: u32,
    pub total_players: u32,
    pub clustered: bool,
    pub host: String,
    pub db_name: String,
    pub tick_interval: Duration,
    pub actions_per_sec: f64,
    /// If true, send input (update_player_input) and rely on server physics; else send position (update_player).
    pub server_physics: bool,
}

/// Run a single simulated player on the current thread (blocking).
///
/// Connects to SpacetimeDB, subscribes to the entity table, then loops:
/// move → send position → maybe send game action → sleep.
pub fn run(
    cfg: StubClientConfig,
    metrics: Arc<Metrics>,
    stop: Arc<AtomicBool>,
    connected: Arc<AtomicU64>,
) {
    let conn = match connect(&cfg) {
        Some(c) => c,
        None => return,
    };

    register_subscription_metrics(&conn, &metrics);
    subscribe(&conn, &cfg, &connected);

    let _bg = conn.run_threaded();
    wait_for_subscription(&connected, cfg.idx);

    run_game_loop(&conn, &cfg, &metrics, &stop);
    cleanup(&conn);
}

// ── Connection ───────────────────────────────────────────────────────────

fn connect(cfg: &StubClientConfig) -> Option<DbConnection> {
    let idx = cfg.idx;

    match DbConnection::builder()
        .with_uri(&cfg.host)
        .with_database_name(&cfg.db_name)
        .on_connect(|_ctx, _identity, _token| {})
        .on_connect_error(move |_ctx, err| {
            if idx == 0 {
                eprintln!("[player {}] connect error: {:?}", idx, err);
            }
        })
        .on_disconnect(move |_ctx, err| {
            if idx == 0 {
                if let Some(e) = err {
                    eprintln!("[player {}] disconnected: {:?}", idx, e);
                }
            }
        })
        .build()
    {
        Ok(c) => Some(c),
        Err(e) => {
            if cfg.idx == 0 {
                eprintln!("[player {}] failed to connect: {:?}", cfg.idx, e);
            }
            None
        }
    }
}

// ── Subscription ─────────────────────────────────────────────────────────

fn register_subscription_metrics(conn: &DbConnection, metrics: &Arc<Metrics>) {
    let m = metrics.clone();
    conn.db.entity().on_update(move |_ctx, _old, _new| {
        m.sub_updates.fetch_add(1, Ordering::Relaxed);
    });

    let m = metrics.clone();
    conn.db.entity().on_insert(move |_ctx, _row| {
        m.sub_inserts.fetch_add(1, Ordering::Relaxed);
    });

    let m = metrics.clone();
    conn.db.entity().on_delete(move |_ctx, _row| {
        m.sub_deletes.fetch_add(1, Ordering::Relaxed);
    });
}

fn subscribe(conn: &DbConnection, cfg: &StubClientConfig, connected: &Arc<AtomicU64>) {
    let flag = connected.clone();
    let idx = cfg.idx;

    conn.subscription_builder()
        .on_applied(move |_ctx| {
            flag.fetch_add(1, Ordering::Relaxed);
        })
        .on_error(move |_ctx, err| {
            if idx == 0 {
                eprintln!("[player {}] subscription error: {:?}", idx, err);
            }
        })
        .subscribe(["SELECT * FROM entity"]);
}

fn wait_for_subscription(connected: &Arc<AtomicU64>, idx: u32) {
    let start = Instant::now();
    while connected.load(Ordering::Relaxed) <= idx as u64
        && start.elapsed() < Duration::from_secs(15)
    {
        std::thread::sleep(Duration::from_millis(50));
    }
}

// ── Game loop ────────────────────────────────────────────────────────────

fn run_game_loop(
    conn: &DbConnection,
    cfg: &StubClientConfig,
    metrics: &Arc<Metrics>,
    stop: &Arc<AtomicBool>,
) {
    let player_uuid = spacetimedb_sdk::Uuid::from_u128(uuid::Uuid::new_v4().as_u128());
    let mut sim = PlayerMovement::new(cfg.idx, cfg.total_players, cfg.clustered);
    let tick_dt = cfg.tick_interval.as_secs_f64();

    let action_interval = if cfg.actions_per_sec > 0.0 {
        Duration::from_micros((1_000_000.0 / cfg.actions_per_sec) as u64)
    } else {
        Duration::from_secs(u64::MAX / 2)
    };
    let mut last_action = Instant::now();
    let mut action_tick: u64 = 0;
    let mut next_tick = Instant::now();
    let mut first_tick = true;

    while !stop.load(Ordering::Relaxed) {
        let now = Instant::now();
        if now < next_tick {
            std::thread::sleep(next_tick - now);
        }
        next_tick += cfg.tick_interval;

        sim.tick(tick_dt, cfg.clustered);
        if cfg.server_physics {
            if first_tick {
                send_position(conn, metrics, player_uuid, &sim);
                first_tick = false;
            } else {
                send_input(conn, metrics, player_uuid, &sim);
            }
        } else {
            send_position(conn, metrics, player_uuid, &sim);
        }

        if cfg.actions_per_sec > 0.0 && last_action.elapsed() >= action_interval {
            last_action = Instant::now();
            action_tick += 1;
            send_game_action(conn, metrics, player_uuid, action_tick, cfg.idx);
        }
    }

    let _ = conn.reducers.remove_player_by_id(player_uuid);
    std::thread::sleep(Duration::from_millis(100));
}

// ── Reducer calls ────────────────────────────────────────────────────────

fn send_position(
    conn: &DbConnection,
    metrics: &Arc<Metrics>,
    player_uuid: spacetimedb_sdk::Uuid,
    sim: &PlayerMovement,
) {
    let entity = Entity {
        entity_id: player_uuid,
        x: sim.x,
        y: sim.y,
        z: sim.z,
    };

    metrics.record_call();
    let t0 = Instant::now();
    let m_ok = metrics.clone();
    let m_err = metrics.clone();

    match conn.reducers.update_player_then(entity, move |_ctx, result| {
        match result {
            Ok(Ok(())) => m_ok.record_reducer_ok(t0.elapsed()),
            _ => m_err.record_reducer_err(),
        }
    }) {
        Ok(()) => {}
        Err(_) => metrics.record_reducer_err(),
    }
}

fn send_input(
    conn: &DbConnection,
    metrics: &Arc<Metrics>,
    player_uuid: spacetimedb_sdk::Uuid,
    sim: &PlayerMovement,
) {
    let (dir_x, dir_z) = sim.direction();
    metrics.record_call();
    let t0 = Instant::now();
    let m_ok = metrics.clone();
    let m_err = metrics.clone();
    match conn.reducers.update_player_input_then(player_uuid, dir_x, dir_z, move |_ctx, result| {
        match result {
            Ok(Ok(())) => m_ok.record_reducer_ok(t0.elapsed()),
            _ => m_err.record_reducer_err(),
        }
    }) {
        Ok(()) => {}
        Err(_) => metrics.record_reducer_err(),
    }
}

fn send_game_action(
    conn: &DbConnection,
    metrics: &Arc<Metrics>,
    player_uuid: spacetimedb_sdk::Uuid,
    action_tick: u64,
    idx: u32,
) {
    let seed = action_tick.wrapping_mul(idx as u64 + 1);
    metrics.record_call();

    match seed % 3 {
        0 => {
            let _ = conn.reducers.pickup_item(
                player_uuid,
                (seed % 10) as u32,
                1 + (seed % 5) as u32,
            );
        }
        1 => {
            let _ = conn.reducers.use_item(player_uuid, (seed % 10) as u32);
        }
        _ => {
            let target = spacetimedb_sdk::Uuid::from_u128(uuid::Uuid::new_v4().as_u128());
            let _ = conn.reducers.player_interact(player_uuid, target, (seed % 4) as u32);
        }
    }
}

fn cleanup(conn: &DbConnection) {
    let _ = conn.disconnect();
}
