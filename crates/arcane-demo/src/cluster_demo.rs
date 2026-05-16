//! Demo-only: automated agents that match Unreal player movement (speed, gravity, jump).
//! Same scale as the playable character: walk in a sustained direction for several seconds, then pick a new direction.
//! Movement params match ArcaneDemoCharacter: MaxWalkSpeed 600 UU/s, JumpZVelocity 400, gravity 980.
//!
//! Projectile support: spawned via GameAction("fire"), despawn after lifetime or on collision.
//! Explosion: on contact, radius query + impulse to all hits. Cross-cluster: impulses route via physics proxies.

use arcane_core::replication_channel::EntityStateEntry;
use arcane_core::Vec3;
use rand::Rng;
use uuid::Uuid;

const TICK_RATE_HZ: u64 = 20;
const TICK_DT: f64 = 1.0 / (TICK_RATE_HZ as f64);
const WORLD_SIZE: f64 = 5000.0;
const WORLD_MARGIN: f64 = 200.0;
/// Match Unreal CharacterMovementComponent default gravity (980 cm/s²). Server uses per-second units.
const GRAVITY: f64 = 980.0;
/// Match ArcaneDemoCharacter JumpZVelocity = 400 → jump height ~82 UU.
const JUMP_VELOCITY: f64 = 400.0;
const GROUND_Y: f64 = 0.0;
const JUMP_CHANCE_DENOM: u64 = 900;

/// Projectile spawning and explosion configuration.
const PROJECTILE_LIFETIME_TICKS: u64 = 200; // ~10s at 20Hz
const PROJECTILE_RADIUS: f64 = 10.0; // collision detection and visual size
const PROJECTILE_INITIAL_SPEED: f64 = 300.0; // UU/s
const EXPLOSION_RADIUS: f64 = 150.0; // sphere query radius for affected entities
const EXPLOSION_IMPULSE_MAGNITUDE: f64 = 500.0; // force applied per entity hit

/// Run speed: match player 600 UU/s. Walk: ~200 UU/s so animations show walk.
const RUN_SPEED_PER_TICK: f64 = 600.0 * TICK_DT;
const WALK_SPEED_PER_TICK: f64 = 200.0 * TICK_DT;
/// How long to stay in current movement state (stand/walk/run) before randomly switching. 3–8 s.
const STATE_DURATION_TICKS_MIN: u64 = 60;
const STATE_DURATION_TICKS_RANGE: u64 = 100;

#[derive(Clone, Copy, PartialEq)]
enum MoveState {
    Stand,
    Walk,
    Run,
}

/// Client action: movement, fire, interact.
#[derive(Clone, Copy, Debug)]
pub enum GameAction {
    Fire { forward_dir: (f64, f64, f64) },
}

/// Projectile entity: spawned by "fire" GameAction, despawns on lifetime or collision.
#[derive(Clone)]
pub struct Projectile {
    pub entity_id: Uuid,
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub vx: f64,
    pub vy: f64,
    pub vz: f64,
    pub lifetime_ticks: u64,
    pub owner_id: Uuid, // don't collide with firing player initially
}

/// Demo agent: position, velocity. Randomly switches between stand / walk / run; walk in one direction per state.
pub struct DemoAgent {
    pub entity_id: Uuid,
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub vx: f64,
    pub vy: f64,
    pub vz: f64,
    walk_dir_x: f64,
    walk_dir_z: f64,
    walk_ticks_remaining: u64,
    move_state: MoveState,
    state_ticks_remaining: u64,
    seed: u32,
}

fn entity_seed(u: &Uuid) -> u32 {
    let b = u.as_bytes();
    u32::from_le_bytes([b[0], b[1], b[2], b[3]])
        .wrapping_add(u32::from_le_bytes([b[4], b[5], b[6], b[7]]))
}

/// Deterministic pseudo-random in [0, 1) from seed and tick (for waypoint picking in tick).
fn drand(seed: u32, tick: u64) -> f64 {
    let t = tick.wrapping_mul(31).wrapping_add(seed as u64);
    let x = (t
        .wrapping_mul(6364136223846793005)
        .wrapping_add(1442695040888963407)) as u32;
    x as f64 / (u32::MAX as f64 + 1.0)
}

fn should_jump(tick: u64, seed: u32) -> bool {
    let s = (tick.wrapping_add(seed as u64 * 7919)) % JUMP_CHANCE_DENOM;
    s < 2
}

/// Spawn circle around world center (world 0..5000, center 2500,2500). 1500 radius = 30 m diameter spread.
const SPAWN_RADIUS: f64 = 1500.0;
const WORLD_CENTER_X: f64 = 2500.0;
const WORLD_CENTER_Z: f64 = 2500.0;

fn pick_walk_direction(a: &mut DemoAgent, tick: u64) {
    let angle = drand(a.seed, tick) * std::f64::consts::TAU;
    a.walk_dir_x = angle.cos();
    a.walk_dir_z = angle.sin();
    a.walk_ticks_remaining = STATE_DURATION_TICKS_MIN
        + (drand(a.seed.wrapping_add(1), tick) * (STATE_DURATION_TICKS_RANGE as f64)) as u64;
}

/// Pick next move state: stand / walk / run (weighted so not everyone runs).
fn pick_move_state(seed: u32, tick: u64) -> (MoveState, u64) {
    let r = drand(seed, tick);
    let state = if r < 0.25 {
        MoveState::Stand
    } else if r < 0.6 {
        MoveState::Walk
    } else {
        MoveState::Run
    };
    let duration = STATE_DURATION_TICKS_MIN
        + (drand(seed.wrapping_add(2), tick) * (STATE_DURATION_TICKS_RANGE as f64)) as u64;
    (state, duration)
}

/// When stress_radius is Some(r), agents spawn and stay in a box [center±r] for "same place" stress.
pub fn create_demo_agents(
    count: u32,
    cluster_id: Uuid,
    stress_radius: Option<f64>,
) -> Vec<DemoAgent> {
    let _ = cluster_id;
    let mut rng = rand::thread_rng();
    let spawn_r = stress_radius.unwrap_or(SPAWN_RADIUS);
    (0..count)
        .map(|i| {
            let n = count.max(1) as f64;
            let angle = (i as f64 / n) * std::f64::consts::TAU + (i as f64 * 0.7);
            let x = WORLD_CENTER_X + spawn_r * angle.cos();
            let z = WORLD_CENTER_Z + spawn_r * angle.sin();
            let entity_id = Uuid::new_v4();
            let seed = entity_seed(&entity_id);
            let dir_angle = rng.gen::<f64>() * std::f64::consts::TAU;
            let walk_dir_x = dir_angle.cos();
            let walk_dir_z = dir_angle.sin();
            let (move_state, state_ticks_remaining) = pick_move_state(seed, 0);
            let walk_ticks_remaining = STATE_DURATION_TICKS_MIN
                + (rng.gen::<f64>() * STATE_DURATION_TICKS_RANGE as f64) as u64;
            DemoAgent {
                entity_id,
                x,
                y: GROUND_Y,
                z,
                vx: 0.0,
                vy: 0.0,
                vz: 0.0,
                walk_dir_x,
                walk_dir_z,
                walk_ticks_remaining,
                move_state,
                state_ticks_remaining,
                seed,
            }
        })
        .collect()
}

/// Tick: gravity + jump; horizontal = stand / walk / run in current direction; switch state every 3–8 s.
/// When stress_radius is Some(r), movement is clamped to [center±r] for "same place" stress.
pub fn tick_demo_agents(agents: &mut [DemoAgent], tick: u64, stress_radius: Option<f64>) {
    let (min_x, max_x, min_z, max_z) = match stress_radius {
        Some(r) => (
            WORLD_CENTER_X - r,
            WORLD_CENTER_X + r,
            WORLD_CENTER_Z - r,
            WORLD_CENTER_Z + r,
        ),
        None => (
            WORLD_MARGIN,
            WORLD_SIZE - WORLD_MARGIN,
            WORLD_MARGIN,
            WORLD_SIZE - WORLD_MARGIN,
        ),
    };
    for a in agents.iter_mut() {
        a.vy -= GRAVITY * TICK_DT;
        a.y += a.vy * TICK_DT;
        if a.y <= GROUND_Y {
            a.y = GROUND_Y;
            a.vy = 0.0;
            if should_jump(tick, a.seed) {
                a.vy = JUMP_VELOCITY;
            }
        }

        if a.state_ticks_remaining == 0 {
            let (state, duration) = pick_move_state(a.seed, tick);
            a.move_state = state;
            a.state_ticks_remaining = duration;
            if state != MoveState::Stand {
                pick_walk_direction(a, tick);
            }
        }
        a.state_ticks_remaining = a.state_ticks_remaining.saturating_sub(1);

        if a.walk_ticks_remaining == 0 && a.move_state != MoveState::Stand {
            pick_walk_direction(a, tick);
        }
        a.walk_ticks_remaining = a.walk_ticks_remaining.saturating_sub(1);

        let speed = match a.move_state {
            MoveState::Stand => 0.0,
            MoveState::Walk => WALK_SPEED_PER_TICK,
            MoveState::Run => RUN_SPEED_PER_TICK,
        };
        a.vx = a.walk_dir_x * speed;
        a.vz = a.walk_dir_z * speed;

        a.x += a.vx;
        a.z += a.vz;

        if a.x < min_x {
            a.x = min_x;
            a.walk_dir_x = a.walk_dir_x.abs();
        } else if a.x > max_x {
            a.x = max_x;
            a.walk_dir_x = -a.walk_dir_x.abs();
        }
        if a.z < min_z {
            a.z = min_z;
            a.walk_dir_z = a.walk_dir_z.abs();
        } else if a.z > max_z {
            a.z = max_z;
            a.walk_dir_z = -a.walk_dir_z.abs();
        }
    }
}

/// Spawn projectile from player position with forward velocity.
pub fn spawn_projectile(
    player_pos: (f64, f64, f64),
    owner_id: Uuid,
    forward: (f64, f64, f64),
) -> Projectile {
    let (px, py, pz) = player_pos;
    let (fx, fy, fz) = forward;
    let len = (fx * fx + fy * fy + fz * fz).sqrt();
    let (fx, fy, fz) = if len > 0.0001 {
        (fx / len, fy / len, fz / len)
    } else {
        (1.0, 0.0, 0.0)
    };

    Projectile {
        entity_id: Uuid::new_v4(),
        x: px,
        y: py,
        z: pz,
        vx: fx * PROJECTILE_INITIAL_SPEED,
        vy: fy * PROJECTILE_INITIAL_SPEED,
        vz: fz * PROJECTILE_INITIAL_SPEED,
        lifetime_ticks: PROJECTILE_LIFETIME_TICKS,
        owner_id,
    }
}

/// Tick projectiles: update position, apply gravity, decrement lifetime.
pub fn tick_projectiles(projectiles: &mut Vec<Projectile>, tick: u64, stress_radius: Option<f64>) {
    let (min_x, max_x, min_z, max_z) = match stress_radius {
        Some(r) => (
            WORLD_CENTER_X - r,
            WORLD_CENTER_X + r,
            WORLD_CENTER_Z - r,
            WORLD_CENTER_Z + r,
        ),
        None => (
            WORLD_MARGIN,
            WORLD_SIZE - WORLD_MARGIN,
            WORLD_MARGIN,
            WORLD_SIZE - WORLD_MARGIN,
        ),
    };

    let mut to_remove = Vec::new();
    for (idx, p) in projectiles.iter_mut().enumerate() {
        p.vy -= GRAVITY * TICK_DT;
        p.y += p.vy * TICK_DT;
        p.x += p.vx * TICK_DT;
        p.z += p.vz * TICK_DT;

        p.lifetime_ticks = p.lifetime_ticks.saturating_sub(1);

        // Out of bounds or expired: mark for removal
        if p.lifetime_ticks == 0
            || p.x < min_x
            || p.x > max_x
            || p.z < min_z
            || p.z > max_z
            || p.y < -100.0
        {
            to_remove.push(idx);
        }
    }

    for idx in to_remove.iter().rev() {
        projectiles.remove(*idx);
    }
}

/// Check collision between projectile and agent (simple sphere overlap).
fn projectile_hits_agent(proj: &Projectile, agent: &DemoAgent) -> bool {
    let dx = proj.x - agent.x;
    let dy = proj.y - agent.y;
    let dz = proj.z - agent.z;
    let dist_sq = dx * dx + dy * dy + dz * dz;
    let collision_dist = PROJECTILE_RADIUS + 30.0; // agent radius ~30
    dist_sq < collision_dist * collision_dist
}

/// Find agents hit by projectiles; remove hit projectiles and return (projectile_pos, hit_count) for explosions.
fn detect_collisions(
    projectiles: &mut Vec<Projectile>,
    agents: &[DemoAgent],
) -> Vec<(Projectile, Vec<Uuid>)> {
    let mut explosions = Vec::new();
    let mut hit_indices = Vec::new();

    for (p_idx, proj) in projectiles.iter().enumerate() {
        let mut hit_agents = Vec::new();
        for agent in agents.iter() {
            if agent.entity_id != proj.owner_id && projectile_hits_agent(proj, agent) {
                hit_agents.push(agent.entity_id);
            }
        }
        if !hit_agents.is_empty() {
            explosions.push((proj.clone(), hit_agents));
            hit_indices.push(p_idx);
        }
    }

    // Remove hit projectiles (in reverse to preserve indices)
    for idx in hit_indices.iter().rev() {
        projectiles.remove(*idx);
    }

    explosions
}

/// Apply explosion impulses to agents within radius. Simple single-cluster version (no cross-cluster yet).
/// Returns (agent_id, impulse_direction) for agents affected.
fn apply_explosions(agents: &mut [DemoAgent], explosions: Vec<(Projectile, Vec<Uuid>)>) {
    for (proj, _hit_agents) in explosions {
        for agent in agents.iter_mut() {
            let dx = agent.x - proj.x;
            let dz = agent.z - proj.z;
            let dist_sq = dx * dx + dz * dz;

            if dist_sq < EXPLOSION_RADIUS * EXPLOSION_RADIUS && dist_sq > 0.0001 {
                let dist = dist_sq.sqrt();
                let nx = dx / dist;
                let nz = dz / dist;

                // Apply radial impulse: magnitude decreases with distance
                let falloff = 1.0 - (dist / EXPLOSION_RADIUS).min(1.0);
                let impulse = EXPLOSION_IMPULSE_MAGNITUDE * falloff;
                agent.vx += nx * impulse * TICK_DT;
                agent.vz += nz * impulse * TICK_DT;
            }
        }
    }
}

/// Converts demo agents to entity entries (cluster_id set by runner). Demo-only.
/// Position/velocity sent as (horizontal1, horizontal2, vertical) so JSON "z" = height; Unreal uses Z for up.
pub fn agents_to_entries(agents: &[DemoAgent], cluster_id: Uuid) -> Vec<EntityStateEntry> {
    agents
        .iter()
        .map(|a| EntityStateEntry {
            entity_id: a.entity_id,
            cluster_id,
            position: Vec3::new(a.x, a.z, a.y),
            velocity: Vec3::new(a.vx, a.vz, a.vy),
            user_data: serde_json::Value::Null,
            local_data: serde_json::Value::Null,
        })
        .collect()
}

/// Converts projectiles to entity entries.
pub fn projectiles_to_entries(
    projectiles: &[Projectile],
    cluster_id: Uuid,
) -> Vec<EntityStateEntry> {
    projectiles
        .iter()
        .map(|p| EntityStateEntry {
            entity_id: p.entity_id,
            cluster_id,
            position: Vec3::new(p.x, p.z, p.y),
            velocity: Vec3::new(p.vx, p.vz, p.vy),
            user_data: serde_json::Value::Null,
            local_data: serde_json::Value::Null,
        })
        .collect()
}
