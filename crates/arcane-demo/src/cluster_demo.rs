//! Demo-only: automated agents that match Unreal player movement (speed, gravity, jump).
//! Same scale as the playable character: walk in a sustained direction for several seconds, then pick a new direction.
//! Movement params match ArcaneDemoCharacter: MaxWalkSpeed 600 UU/s, JumpZVelocity 400, gravity 980.
//!
//! Group behaviour (additive — existing API unchanged):
//! - `create_grouped_agents` + `tick_grouped_agents` for cohesion + encounter behaviour.
//! - Agents with `group_id = None` behave identically to the original random-walk model.

use arcane_core::replication_channel::EntityStateEntry;
use arcane_core::Vec3;
use rand::Rng;
use std::collections::HashMap;
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

/// Run speed: match player 600 UU/s. Walk: ~200 UU/s so animations show walk.
const RUN_SPEED_PER_TICK: f64 = 600.0 * TICK_DT;
const WALK_SPEED_PER_TICK: f64 = 200.0 * TICK_DT;
/// How long to stay in current movement state (stand/walk/run) before randomly switching. 3–8 s.
const STATE_DURATION_TICKS_MIN: u64 = 60;
const STATE_DURATION_TICKS_RANGE: u64 = 100;

/// Cohesion weight: fraction of direction toward group centroid vs personal random direction.
const COHESION_WEIGHT: f64 = 0.6;
/// Encounter trigger interval range in ticks (30–60 s at 20 Hz = 600–1200 ticks).
const ENCOUNTER_INTERVAL_MIN: u64 = 600;
const ENCOUNTER_INTERVAL_RANGE: u64 = 600;
/// Encounter duration range in ticks (20–40 s at 20 Hz = 400–800 ticks).
const ENCOUNTER_DURATION_MIN: u64 = 400;
const ENCOUNTER_DURATION_RANGE: u64 = 400;

#[derive(Clone, Copy, PartialEq)]
enum MoveState {
    Stand,
    Walk,
    Run,
}

/// Demo agent: position, velocity. Randomly switches between stand / walk / run; walk in one direction per state.
/// `group_id = None` means ungrouped — identical behaviour to the original random-walk model.
pub struct DemoAgent {
    pub entity_id: Uuid,
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub vx: f64,
    pub vy: f64,
    pub vz: f64,
    pub group_id: Option<Uuid>,
    walk_dir_x: f64,
    walk_dir_z: f64,
    walk_ticks_remaining: u64,
    move_state: MoveState,
    state_ticks_remaining: u64,
    seed: u32,
}

/// Per-group dynamic state computed fresh each tick from agent positions.
pub struct GroupState {
    pub centroid_x: f64,
    pub centroid_z: f64,
    /// If Some, this group is in an encounter — steer toward this target centroid.
    pub encounter_target: Option<(f64, f64)>,
}

/// Persistent encounter cooldown state per group. Survives across ticks.
pub struct EncounterTracker {
    /// group_id → ticks remaining in current encounter (0 = wandering)
    cooldowns: HashMap<Uuid, u32>,
}

impl EncounterTracker {
    pub fn new() -> Self {
        Self {
            cooldowns: HashMap::new(),
        }
    }

    pub fn tick(&mut self) {
        self.cooldowns
            .values_mut()
            .for_each(|v| *v = v.saturating_sub(1));
        self.cooldowns.retain(|_, v| *v > 0);
    }

    pub fn is_in_encounter(&self, group_id: Uuid) -> bool {
        self.cooldowns.contains_key(&group_id)
    }

    /// Start an encounter for `group_id`. Duration is deterministic from group_id seed + tick.
    pub fn start_encounter(&mut self, group_id: Uuid, tick: u64) {
        let group_seed = uuid_to_seed(group_id);
        let duration = ENCOUNTER_DURATION_MIN
            + (drand(group_seed, tick.wrapping_add(17)) * ENCOUNTER_DURATION_RANGE as f64) as u64;
        self.cooldowns.insert(group_id, duration as u32);
    }
}

impl Default for EncounterTracker {
    fn default() -> Self {
        Self::new()
    }
}

fn entity_seed(u: &Uuid) -> u32 {
    let b = u.as_bytes();
    u32::from_le_bytes([b[0], b[1], b[2], b[3]])
        .wrapping_add(u32::from_le_bytes([b[4], b[5], b[6], b[7]]))
}

fn uuid_to_seed(u: Uuid) -> u32 {
    entity_seed(&u)
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
                group_id: None,
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

/// Create agents assigned to groups of `group_size`.
/// Group UUIDs are deterministic: group i = Uuid::from_u128(i as u128).
/// Group members spawn adjacent on the circle; groups are spread evenly around it.
/// `group_size = 0` is treated as `count` (all agents in one group).
pub fn create_grouped_agents(
    count: u32,
    group_size: u32,
    stress_radius: Option<f64>,
) -> Vec<DemoAgent> {
    let gsize = if group_size == 0 {
        count.max(1)
    } else {
        group_size
    };
    let spawn_r = stress_radius.unwrap_or(SPAWN_RADIUS);
    let n = count.max(1) as f64;
    (0..count)
        .map(|i| {
            let group_idx = i / gsize;
            let group_id = Uuid::from_u128(group_idx as u128);
            let entity_id = Uuid::new_v4();
            let seed = entity_seed(&entity_id);
            let angle = (i as f64 / n) * std::f64::consts::TAU;
            let x = WORLD_CENTER_X + spawn_r * angle.cos();
            let z = WORLD_CENTER_Z + spawn_r * angle.sin();
            let (move_state, state_ticks_remaining) = pick_move_state(seed, 0);
            let walk_ticks_remaining = STATE_DURATION_TICKS_MIN;
            DemoAgent {
                entity_id,
                x,
                y: GROUND_Y,
                z,
                vx: 0.0,
                vy: 0.0,
                vz: 0.0,
                group_id: Some(group_id),
                walk_dir_x: angle.cos(),
                walk_dir_z: angle.sin(),
                walk_ticks_remaining,
                move_state,
                state_ticks_remaining,
                seed,
            }
        })
        .collect()
}

/// Compute current group centroids and encounter targets from agent positions.
/// Only agents with `group_id = Some(...)` are included.
pub fn compute_group_states(
    agents: &[DemoAgent],
    tracker: &EncounterTracker,
) -> HashMap<Uuid, GroupState> {
    // Accumulate sum and count per group
    let mut sums: HashMap<Uuid, (f64, f64, u32)> = HashMap::new();
    for a in agents {
        if let Some(gid) = a.group_id {
            let e = sums.entry(gid).or_insert((0.0, 0.0, 0));
            e.0 += a.x;
            e.1 += a.z;
            e.2 += 1;
        }
    }

    let centroids: HashMap<Uuid, (f64, f64)> = sums
        .iter()
        .map(|(&gid, &(sx, sz, n))| (gid, (sx / n as f64, sz / n as f64)))
        .collect();

    centroids
        .iter()
        .map(|(&gid, &(cx, cz))| {
            let encounter_target = if tracker.is_in_encounter(gid) {
                // Find nearest other group centroid as the target
                centroids
                    .iter()
                    .filter(|(&other, _)| other != gid)
                    .min_by(|(_, &(ax, az)), (_, &(bx, bz))| {
                        let da = (ax - cx) * (ax - cx) + (az - cz) * (az - cz);
                        let db = (bx - cx) * (bx - cx) + (bz - cz) * (bz - cz);
                        da.partial_cmp(&db).unwrap_or(std::cmp::Ordering::Equal)
                    })
                    .map(|(_, &(tx, tz))| (tx, tz))
            } else {
                None
            };
            (
                gid,
                GroupState {
                    centroid_x: cx,
                    centroid_z: cz,
                    encounter_target,
                },
            )
        })
        .collect()
}

/// Tick agents with group cohesion and encounter steering.
/// Agents with `group_id = None` behave identically to `tick_demo_agents`.
/// Call `compute_group_states` before this each tick.
/// Call `tracker.tick()` after this each tick.
pub fn tick_grouped_agents(
    agents: &mut [DemoAgent],
    tick: u64,
    group_states: &HashMap<Uuid, GroupState>,
    tracker: &mut EncounterTracker,
    stress_radius: Option<f64>,
) {
    // Check encounter triggers for each group (deterministic from group seed + tick).
    // A group triggers an encounter when tick is a multiple of its interval.
    let group_ids: Vec<Uuid> = {
        let mut seen = std::collections::HashSet::new();
        agents
            .iter()
            .filter_map(|a| a.group_id)
            .filter(|&g| seen.insert(g))
            .collect()
    };
    for gid in group_ids {
        if !tracker.is_in_encounter(gid) {
            let gseed = uuid_to_seed(gid);
            let interval = ENCOUNTER_INTERVAL_MIN
                + (drand(gseed, tick.wrapping_add(3)) * ENCOUNTER_INTERVAL_RANGE as f64) as u64;
            // Spread trigger check across groups using group seed to avoid all triggering at once.
            let offset = (gseed as u64) % interval.max(1);
            if interval > 0 && tick.wrapping_add(offset).is_multiple_of(interval) {
                tracker.start_encounter(gid, tick);
            }
        }
    }

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
        // Gravity + jump (identical for all agents)
        a.vy -= GRAVITY * TICK_DT;
        a.y += a.vy * TICK_DT;
        if a.y <= GROUND_Y {
            a.y = GROUND_Y;
            a.vy = 0.0;
            if should_jump(tick, a.seed) {
                a.vy = JUMP_VELOCITY;
            }
        }

        // State machine (identical for all agents)
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

        // Direction: grouped agents blend toward centroid or encounter target
        if let Some(gid) = a.group_id {
            if let Some(gs) = group_states.get(&gid) {
                let (target_x, target_z) = gs
                    .encounter_target
                    .unwrap_or((gs.centroid_x, gs.centroid_z));

                let dx = target_x - a.x;
                let dz = target_z - a.z;
                let dist = (dx * dx + dz * dz).sqrt();
                if dist > 1.0 {
                    let toward_x = dx / dist;
                    let toward_z = dz / dist;
                    // Blend: COHESION_WEIGHT toward group target, rest personal direction
                    a.walk_dir_x =
                        COHESION_WEIGHT * toward_x + (1.0 - COHESION_WEIGHT) * a.walk_dir_x;
                    a.walk_dir_z =
                        COHESION_WEIGHT * toward_z + (1.0 - COHESION_WEIGHT) * a.walk_dir_z;
                    // Renormalize
                    let len = (a.walk_dir_x * a.walk_dir_x + a.walk_dir_z * a.walk_dir_z).sqrt();
                    if len > 1e-6 {
                        a.walk_dir_x /= len;
                        a.walk_dir_z /= len;
                    }
                }
            }
        }

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

/// Converts demo agents to entity entries (cluster_id set by runner). Demo-only.
/// Position/velocity sent as (horizontal1, horizontal2, vertical) so JSON "z" = height; Unreal uses Z for up.
pub fn agents_to_entries(agents: &[DemoAgent], cluster_id: Uuid) -> Vec<EntityStateEntry> {
    agents
        .iter()
        .map(|a| {
            EntityStateEntry::new(
                a.entity_id,
                cluster_id,
                Vec3::new(a.x, a.z, a.y),
                Vec3::new(a.vx, a.vz, a.vy),
            )
        })
        .collect()
}
