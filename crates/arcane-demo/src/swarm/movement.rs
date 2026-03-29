//! Deterministic player movement simulation.
//!
//! Reproduces the kind of movement an Unreal character controller would generate:
//! walk in a direction, periodically turn, bounce off world boundaries.

use std::f64::consts::TAU;

pub const WORLD_SIZE: f64 = 5000.0;
pub const WORLD_CENTER: f64 = 2500.0;
const MOVE_SPEED: f64 = 600.0;
const CLUSTER_RADIUS: f64 = 300.0;

/// Simulated player position and velocity, updated each tick.
pub struct PlayerMovement {
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub vx: f64,
    pub vy: f64,
    pub vz: f64,
    dir_x: f64,
    dir_z: f64,
    ticks_until_turn: u32,
    seed: u8,
}

impl PlayerMovement {
    /// Spawn a player at a deterministic position on a ring around world center.
    /// `clustered` packs all players into a small area (town square scenario).
    pub fn new(idx: u32, total: u32, clustered: bool) -> Self {
        let angle = (idx as f64 / total.max(1) as f64) * TAU;
        let radius = if clustered {
            CLUSTER_RADIUS
        } else {
            WORLD_SIZE * 0.35
        };
        Self {
            x: WORLD_CENTER + radius * angle.cos(),
            y: 0.0,
            z: WORLD_CENTER + radius * angle.sin(),
            vx: 0.0,
            vy: 0.0,
            vz: 0.0,
            dir_x: angle.cos(),
            dir_z: angle.sin(),
            ticks_until_turn: 60 + (idx % 80),
            seed: (idx % 256) as u8,
        }
    }

    /// Current movement direction (unit vector in x/z). Use for server-physics input.
    pub fn direction(&self) -> (f64, f64) {
        (self.dir_x, self.dir_z)
    }

    /// Advance one simulation tick. `dt` is seconds per tick (e.g. 0.05 for 20 Hz).
    pub fn tick(&mut self, dt: f64, clustered: bool) {
        self.ticks_until_turn = self.ticks_until_turn.saturating_sub(1);
        if self.ticks_until_turn == 0 {
            let a = (self.seed as f64 * 0.1 + self.x * 0.001).sin() * TAU;
            self.dir_x = a.cos();
            self.dir_z = a.sin();
            self.ticks_until_turn = 40 + ((self.seed as u32) % 80);
        }

        let speed = MOVE_SPEED * dt;
        self.vx = self.dir_x * speed;
        self.vz = self.dir_z * speed;
        self.x += self.vx;
        self.z += self.vz;

        let (lo, hi) = if clustered {
            (
                WORLD_CENTER - CLUSTER_RADIUS * 2.0,
                WORLD_CENTER + CLUSTER_RADIUS * 2.0,
            )
        } else {
            (200.0, WORLD_SIZE - 200.0)
        };

        if self.x < lo || self.x > hi {
            self.dir_x = -self.dir_x;
            self.x = self.x.clamp(lo, hi);
        }
        if self.z < lo || self.z > hi {
            self.dir_z = -self.dir_z;
            self.z = self.z.clamp(lo, hi);
        }
    }
}
