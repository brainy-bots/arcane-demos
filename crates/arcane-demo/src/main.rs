//! Demo: clustering by **interaction likelihood** (primary) and **max load per server** (split when over).
//! Who interacts (guild/party/enemies in range) drives grouping; position only defines "in range".
//! When many entities interact in one place we SPLIT into more servers (load cap), not merge into one.

use arcane_core::Vec3;
use arcane_infra::ClusterManager;
use rand::Rng;
use std::collections::HashMap;
use std::env;
use std::fs;
use std::path::Path;
use uuid::Uuid;

const WORLD_W: f64 = 400.0;
const WORLD_D: f64 = 400.0;
const N_TICKS: u32 = 500;
const R_INTERACT: f64 = 70.0;
const MAX_LOAD: usize = 5;
const HYST_TICKS: u8 = 2;
const STEER: f64 = 0.025;
const WANDER: f64 = 0.4;
const DAMP: f64 = 0.92;
const MAX_SPEED: f64 = 2.2;
const SOLO_WANDER: f64 = 0.8;   // solo entities: random step size per tick

#[derive(Clone, Copy)]
enum PathKind {
    Linear { speed: f64 },           // speed 1.0 = full path in N_TICKS; 0.5 = half speed, etc.
    Wander { drift: f64 },          // center random-walks
    Curve { period_ticks: f64 },    // orbit around (start+end)/2, radius from start→end
    Meander { speed: f64, wobble: f64 }, // linear + perpendicular wobble
}

/// Group: guild, party, size, start/end, and how the center moves (not all linear).
struct GroupSpec {
    guild: u8,
    party: u8,
    n_entities: usize,
    start_x: f64,
    start_z: f64,
    end_x: f64,
    end_z: f64,
    path: PathKind,
}

/// Solo player: one entity, no formation, wanders on its own.
struct SoloSpec {
    guild: u8,
    party: u8,
    start_x: f64,
    start_z: f64,
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let out_dir = env::args().nth(1).unwrap_or_else(|| ".".to_string());
    let out_path = Path::new(&out_dir);
    let mut manager = ClusterManager::with_defaults();
    let mut rng = rand::thread_rng();

    let enemies: Vec<(u8, u8)> = vec![(0, 1), (1, 2)];
    use PathKind::*;
    let groups: Vec<GroupSpec> = vec![
        GroupSpec { guild: 0, party: 0, n_entities: 4, start_x: 60.0, start_z: 60.0, end_x: 280.0, end_z: 280.0, path: Linear { speed: 0.9 } },
        GroupSpec { guild: 1, party: 1, n_entities: 4, start_x: 340.0, start_z: 340.0, end_x: 120.0, end_z: 120.0, path: Linear { speed: 1.2 } },
        GroupSpec { guild: 2, party: 2, n_entities: 3, start_x: 200.0, start_z: 40.0, end_x: 200.0, end_z: 360.0, path: Linear { speed: 0.6 } },
        GroupSpec { guild: 0, party: 3, n_entities: 3, start_x: 80.0, start_z: 320.0, end_x: 260.0, end_z: 80.0, path: Meander { speed: 0.7, wobble: 25.0 } },
        GroupSpec { guild: 3, party: 4, n_entities: 4, start_x: 320.0, start_z: 60.0, end_x: 80.0, end_z: 340.0, path: Linear { speed: 1.0 } },
        GroupSpec { guild: 1, party: 5, n_entities: 3, start_x: 100.0, start_z: 220.0, end_x: 280.0, end_z: 180.0, path: Curve { period_ticks: 400.0 } },
        GroupSpec { guild: 2, party: 6, n_entities: 4, start_x: 40.0, start_z: 200.0, end_x: 360.0, end_z: 200.0, path: Linear { speed: 0.5 } },
        GroupSpec { guild: 4, party: 7, n_entities: 3, start_x: 350.0, start_z: 200.0, end_x: 50.0, end_z: 200.0, path: Meander { speed: 0.4, wobble: 40.0 } },
        GroupSpec { guild: 0, party: 8, n_entities: 3, start_x: 150.0, start_z: 150.0, end_x: 250.0, end_z: 250.0, path: Wander { drift: 1.2 } },
        GroupSpec { guild: 1, party: 9, n_entities: 3, start_x: 250.0, start_z: 250.0, end_x: 150.0, end_z: 150.0, path: Wander { drift: 1.0 } },
        GroupSpec { guild: 3, party: 10, n_entities: 4, start_x: 50.0, start_z: 300.0, end_x: 350.0, end_z: 100.0, path: Linear { speed: 1.1 } },
        GroupSpec { guild: 2, party: 11, n_entities: 3, start_x: 300.0, start_z: 50.0, end_x: 100.0, end_z: 350.0, path: Curve { period_ticks: 350.0 } },
        GroupSpec { guild: 4, party: 12, n_entities: 4, start_x: 180.0, start_z: 180.0, end_x: 220.0, end_z: 220.0, path: Wander { drift: 0.8 } },
        GroupSpec { guild: 0, party: 13, n_entities: 3, start_x: 100.0, start_z: 100.0, end_x: 300.0, end_z: 300.0, path: Linear { speed: 0.3 } },
        GroupSpec { guild: 5, party: 14, n_entities: 3, start_x: 200.0, start_z: 50.0, end_x: 200.0, end_z: 350.0, path: Linear { speed: 0.8 } },
        GroupSpec { guild: 5, party: 15, n_entities: 4, start_x: 50.0, start_z: 200.0, end_x: 350.0, end_z: 200.0, path: Meander { speed: 0.6, wobble: 30.0 } },
    ];
    let solos: Vec<SoloSpec> = (0..18).map(|_| SoloSpec {
        guild: rng.gen_range(0..6u8),
        party: rng.gen_range(16..24u8),
        start_x: rng.gen_range(20.0..WORLD_W - 20.0),
        start_z: rng.gen_range(20.0..WORLD_D - 20.0),
    }).collect();

    let formation: Vec<(f64, f64)> = vec![
        (0.0, 0.0), (-14.0, -6.0), (14.0, -6.0), (-14.0, 6.0), (14.0, 6.0),
        (-8.0, -10.0), (8.0, -10.0), (-8.0, 10.0), (8.0, 10.0),
    ];
    let n_groups = groups.len();
    let mut entities: Vec<Entity> = Vec::new();
    for (g_idx, g) in groups.iter().enumerate() {
        for slot in 0..g.n_entities {
            let (dx, dz) = formation[slot % formation.len()];
            entities.push(Entity {
                id: Uuid::new_v4(),
                x: (g.start_x + dx).clamp(0.0, WORLD_W),
                z: (g.start_z + dz).clamp(0.0, WORLD_D),
                vx: rng.gen_range(-0.5..0.5),
                vz: rng.gen_range(-0.5..0.5),
                group_index: g_idx,
                formation_slot: slot,
                guild: g.guild,
                party: g.party,
                cluster_id: Uuid::new_v4(),
                proposed_cluster_id: Uuid::nil(),
                proposed_ticks: 0,
            });
        }
    }
    for s in &solos {
        entities.push(Entity {
            id: Uuid::new_v4(),
            x: s.start_x,
            z: s.start_z,
            vx: rng.gen_range(-0.3..0.3),
            vz: rng.gen_range(-0.3..0.3),
            group_index: n_groups,
            formation_slot: 0,
            guild: s.guild,
            party: s.party,
            cluster_id: Uuid::new_v4(),
            proposed_cluster_id: Uuid::nil(),
            proposed_ticks: 0,
        });
    }

    let mut group_centers: Vec<(f64, f64)> = groups.iter().map(|g| (g.start_x, g.start_z)).collect();

    let mut component_id_cache: HashMap<Vec<u128>, Uuid> = HashMap::new();
    let mut frames: Vec<serde_json::Value> = Vec::with_capacity(N_TICKS as usize);

    let pi = std::f64::consts::PI;
    for tick in 0..N_TICKS {
        let t = tick as f64;
        let nt = N_TICKS as f64;

        // 1) Update each group's center from its path (not all linear)
        for (g_idx, g) in groups.iter().enumerate() {
            let (cx, cz) = match g.path {
                PathKind::Linear { speed } => {
                    let progress = (t / nt * speed).min(1.0);
                    (
                        g.start_x + progress * (g.end_x - g.start_x),
                        g.start_z + progress * (g.end_z - g.start_z),
                    )
                }
                PathKind::Wander { drift } => {
                    let (cx, cz) = group_centers[g_idx];
                    (
                        (cx + rng.gen_range(-drift..drift)).clamp(0.0, WORLD_W),
                        (cz + rng.gen_range(-drift..drift)).clamp(0.0, WORLD_D),
                    )
                }
                PathKind::Curve { period_ticks } => {
                    let mid_x = (g.start_x + g.end_x) * 0.5;
                    let mid_z = (g.start_z + g.end_z) * 0.5;
                    let radius = ((g.end_x - g.start_x).powi(2) + (g.end_z - g.start_z).powi(2)).sqrt() * 0.5;
                    let angle = 2.0 * pi * t / period_ticks;
                    (mid_x + radius * angle.cos(), mid_z + radius * angle.sin())
                }
                PathKind::Meander { speed, wobble } => {
                    let progress = (t / nt * speed).min(1.0);
                    let dx = g.end_x - g.start_x;
                    let dz = g.end_z - g.start_z;
                    let perp_x = -dz;
                    let perp_z = dx;
                    let len = (perp_x * perp_x + perp_z * perp_z).sqrt().max(1e-6);
                    let perp_x = perp_x / len * wobble * (2.0 * pi * t / 80.0).sin();
                    let perp_z = perp_z / len * wobble * (2.0 * pi * t / 80.0).sin();
                    (
                        g.start_x + progress * dx + perp_x,
                        g.start_z + progress * dz + perp_z,
                    )
                }
            };
            group_centers[g_idx] = (cx, cz);
        }

        // 2) Move entities: group members steer toward center+formation; solos wander
        for e in entities.iter_mut() {
            if e.group_index < n_groups {
                let (cx, cz) = group_centers[e.group_index];
                let (dx, dz) = formation[e.formation_slot % formation.len()];
                let target_x = (cx + dx).clamp(0.0, WORLD_W);
                let target_z = (cz + dz).clamp(0.0, WORLD_D);
                e.vx += (target_x - e.x) * STEER + rng.gen_range(-WANDER..WANDER);
                e.vz += (target_z - e.z) * STEER + rng.gen_range(-WANDER..WANDER);
            } else {
                e.vx += rng.gen_range(-SOLO_WANDER..SOLO_WANDER);
                e.vz += rng.gen_range(-SOLO_WANDER..SOLO_WANDER);
            }
            e.vx *= DAMP;
            e.vz *= DAMP;
            let speed_sq = e.vx * e.vx + e.vz * e.vz;
            if speed_sq > MAX_SPEED * MAX_SPEED {
                let s = MAX_SPEED / speed_sq.sqrt();
                e.vx *= s;
                e.vz *= s;
            }
            e.x = (e.x + e.vx).clamp(0.0, WORLD_W);
            e.z = (e.z + e.vz).clamp(0.0, WORLD_D);
        }

        // 2) Build interaction graph: edge if distance < R and (same_guild OR same_party OR enemies)
        let n = entities.len();
        let mut uf = UnionFind::new(n);
        for i in 0..n {
            for j in (i + 1)..n {
                let dist_sq = (entities[i].x - entities[j].x).powi(2) + (entities[i].z - entities[j].z).powi(2);
                if dist_sq > R_INTERACT * R_INTERACT {
                    continue;
                }
                let interact = entities[i].guild == entities[j].guild
                    || entities[i].party == entities[j].party
                    || enemies
                        .iter()
                        .any(|&(a, b)| {
                            (entities[i].guild == a && entities[j].guild == b)
                                || (entities[i].guild == b && entities[j].guild == a)
                        });
                if interact {
                    uf.union(i, j);
                }
            }
        }

        // 3) Assign cluster_id per entity: interaction groups (components) are SPLIT by max load.
        //    So "everyone together" => multiple clusters (multiple servers), not one.
        let mut root_to_entities: HashMap<usize, Vec<usize>> = HashMap::new();
        for i in 0..n {
            let r = uf.find(i);
            root_to_entities.entry(r).or_default().push(i);
        }
        let mut component_ids: Vec<Uuid> = vec![Uuid::nil(); n];
        for (_, indices) in &root_to_entities {
            if indices.len() <= MAX_LOAD {
                let mut key: Vec<u128> = indices.iter().map(|&i| entity_bits(&entities[i].id)).collect();
                key.sort();
                let cid = *component_id_cache
                    .entry(key)
                    .or_insert_with(Uuid::new_v4);
                for &i in indices {
                    component_ids[i] = cid;
                }
            } else {
                // Split by load: pack by party (keep same party together) into buckets of at most MAX_LOAD
                let mut by_party: HashMap<u8, Vec<usize>> = indices
                    .iter()
                    .fold(HashMap::new(), |mut m, &i| {
                        m.entry(entities[i].party).or_default().push(i);
                        m
                    });
                let mut sub_clusters: Vec<Vec<usize>> = Vec::new();
                let mut current: Vec<usize> = Vec::new();
                let mut party_keys: Vec<u8> = by_party.keys().copied().collect();
                party_keys.sort();
                for party in party_keys {
                    let list = by_party.get_mut(&party).unwrap();
                    for idx in std::mem::take(list) {
                        if current.len() >= MAX_LOAD {
                            sub_clusters.push(std::mem::take(&mut current));
                        }
                        current.push(idx);
                    }
                }
                if !current.is_empty() {
                    sub_clusters.push(current);
                }
                for sub in &sub_clusters {
                    let mut key: Vec<u128> = sub.iter().map(|&i| entity_bits(&entities[i].id)).collect();
                    key.sort();
                    let cid = *component_id_cache
                        .entry(key)
                        .or_insert_with(Uuid::new_v4);
                    for &i in sub {
                        component_ids[i] = cid;
                    }
                }
            }
        }

        // 4) Hysteresis: proposed_cluster = component_ids[i]; only switch after HYST_TICKS
        for (i, e) in entities.iter_mut().enumerate() {
            let new_cid = component_ids[i];
            if new_cid == e.cluster_id {
                e.proposed_ticks = 0;
                continue;
            }
            if new_cid == e.proposed_cluster_id {
                e.proposed_ticks += 1;
                if e.proposed_ticks >= HYST_TICKS {
                    e.cluster_id = new_cid;
                    e.proposed_ticks = 0;
                }
            } else {
                e.proposed_cluster_id = new_cid;
                e.proposed_ticks = 1;
            }
        }

        // 5) Feed manager and build frame
        for e in &entities {
            manager.update_entity(e.id, e.cluster_id, Vec3::new(e.x, 0.0, e.z));
        }
        manager.run_evaluation_cycle()?;
        let snapshot = manager.snapshot_for_view();

        let clusters_json: Vec<_> = snapshot
            .iter()
            .map(|g| {
                serde_json::json!({
                    "id": g.cluster_id.to_string(),
                    "centroid": { "x": g.centroid.x, "y": g.centroid.z },
                    "spread_radius": g.spread_radius,
                    "entity_count": g.entity_count,
                })
            })
            .collect();
        let entities_json: Vec<_> = entities
            .iter()
            .map(|e| {
                serde_json::json!({
                    "id": e.id.to_string(),
                    "x": e.x,
                    "z": e.z,
                    "guild": e.guild,
                    "party": e.party,
                    "cluster_id": e.cluster_id.to_string(),
                })
            })
            .collect();
        frames.push(serde_json::json!({
            "tick": tick,
            "clusters": clusters_json,
            "entities": entities_json,
        }));
    }

    let state = serde_json::json!({
        "frames": frames,
        "world": { "w": WORLD_W, "d": WORLD_D },
        "note": "Interaction likelihood (guild/party/enemies in range) groups entities; max load per server forces SPLIT when many are together — more servers, not one big cluster."
    });
    let state_str = serde_json::to_string_pretty(&state)?;
    fs::write(out_path.join("state.json"), &state_str)?;
    eprintln!("Wrote state.json ({} frames)", frames.len());

    let html = make_viz_html(&state_str);
    fs::write(out_path.join("viz.html"), html)?;
    eprintln!("Wrote state.json, viz.html ({} frames).", frames.len());

    Ok(())
}

fn entity_bits(u: &Uuid) -> u128 {
    let b = u.as_bytes();
    u128::from_le_bytes(b[0..16].try_into().unwrap())
}

#[derive(Clone)]
struct Entity {
    id: Uuid,
    x: f64,
    z: f64,
    vx: f64,
    vz: f64,
    group_index: usize,
    formation_slot: usize,
    guild: u8,
    party: u8,
    cluster_id: Uuid,
    proposed_cluster_id: Uuid,
    proposed_ticks: u8,
}

struct UnionFind {
    parent: Vec<usize>,
}

impl UnionFind {
    fn new(n: usize) -> Self {
        Self { parent: (0..n).collect() }
    }
    fn find(&mut self, i: usize) -> usize {
        if self.parent[i] != i {
            self.parent[i] = self.find(self.parent[i]);
        }
        self.parent[i]
    }
    fn union(&mut self, i: usize, j: usize) {
        let a = self.find(i);
        let b = self.find(j);
        if a != b {
            self.parent[a] = b;
        }
    }
}

fn make_viz_html(state_json: &str) -> String {
    let escaped: String = state_json
        .replace('\\', "\\\\")
        .replace('"', "\\\"")
        .replace("</script>", "<\\/script>")
        .replace('\r', "")
        .replace('\n', " ");
    const TEMPLATE: &str = r#"<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Arcane clustering — interaction + relative position</title>
  <style>
    body { font-family: system-ui, sans-serif; margin: 1rem; }
    h1 { margin-bottom: 0.25rem; }
    #info { color: #666; margin-bottom: 0.5rem; }
    #note, #legend { font-size: 0.9rem; color: #888; margin-bottom: 0.25rem; }
    #controls { margin-bottom: 0.5rem; }
    canvas { border: 1px solid #ccc; background: #1a1a1a; display: block; }
  </style>
</head>
<body>
  <h1>Arcane clustering</h1>
  <p id="note">Many groups (linear, wander, curve, meander) + isolated solo players. Interaction + max load drive clustering. Dot fill = cluster; border = guild.</p>
  <p id="legend">Groups move at different speeds and patterns; solos wander alone. More clusters when crowded (load cap).</p>
  <p id="info"></p>
  <div id="controls">
    <button id="play">Play</button>
    <button id="pause">Pause</button>
    <span>Frame: <span id="frameNum">0</span> / <span id="totalFrames">0</span></span>
    <input type="range" id="slider" min="0" max="100" value="0" style="width: 300px;">
  </div>
  <canvas id="canvas" width="600" height="600"></canvas>
  <script>
    const stateJson = "__STATE_JSON__";
    const data = JSON.parse(stateJson);
    const frames = data.frames || [];
    const world = data.world || { w: 400, d: 400 };
    const worldW = world.w || 400;
    const worldD = world.d || 400;
    let currentFrame = 0, playing = true, frameIntervalMs = 80, lastAdvance = Date.now();
    const canvas = document.getElementById('canvas');
    const ctx = canvas.getContext('2d');
    const scale = Math.min(canvas.width / worldW, canvas.height / worldD);
    const originX = (canvas.width - worldW * scale) / 2;
    const originZ = (canvas.height - worldD * scale) / 2;
    function toScreenX(x) { return originX + x * scale; }
    function toScreenZ(z) { return originZ + (worldD - z) * scale; }
    function hashToHue(s) { let h = 0; for (let i = 0; i < s.length; i++) h = ((h << 5) - h) + s.charCodeAt(i) | 0; return Math.abs(h) % 360; }
    function drawFrame(idx) {
      if (idx < 0 || idx >= frames.length) return;
      const frame = frames[idx];
      ctx.fillStyle = '#1a1a1a';
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      const clusterColors = {};
      (frame.clusters || []).forEach(c => { clusterColors[c.id] = 'hsl(' + hashToHue(c.id) + ', 70%, 55%)'; });
      for (const c of (frame.clusters || [])) {
        const x = toScreenX(c.centroid.x), z = toScreenZ(c.centroid.y);
        const r = Math.max(3, (c.spread_radius || 0) * scale + 6);
        const hue = hashToHue(c.id);
        ctx.strokeStyle = 'hsla(' + hue + ', 70%, 55%, 0.5)';
        ctx.fillStyle = 'hsla(' + hue + ', 50%, 25%, 0.2)';
        ctx.beginPath(); ctx.arc(x, z, r, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
      }
      const guildBorder = ['#e74c3c', '#3498db', '#2ecc71', '#9b59b6', '#f39c12', '#1abc9c'];
      for (const e of (frame.entities || [])) {
        const x = toScreenX(e.x), z = toScreenZ(e.z);
        const color = clusterColors[e.cluster_id] || 'hsl(0,0%,60%)';
        ctx.fillStyle = color;
        ctx.beginPath(); ctx.arc(x, z, 4, 0, Math.PI * 2); ctx.fill();
        ctx.strokeStyle = guildBorder[e.guild] || 'rgba(255,255,255,0.4)';
        ctx.lineWidth = 1.5;
        ctx.stroke();
      }
      document.getElementById('frameNum').textContent = frame.tick;
      document.getElementById('info').textContent = 'Tick ' + frame.tick + ' — ' + (frame.clusters || []).length + ' cluster(s), ' + (frame.entities || []).length + ' entities. Fill = cluster; border = guild.';
    }
    function tick() {
      if (playing && Date.now() - lastAdvance >= frameIntervalMs) {
        lastAdvance = Date.now();
        currentFrame = (currentFrame + 1) % frames.length;
        document.getElementById('slider').value = currentFrame;
        drawFrame(currentFrame);
      }
      requestAnimationFrame(tick);
    }
    document.getElementById('totalFrames').textContent = frames.length;
    document.getElementById('slider').max = Math.max(0, frames.length - 1);
    document.getElementById('slider').oninput = function() { currentFrame = parseInt(this.value, 10); drawFrame(currentFrame); };
    document.getElementById('play').onclick = function() { playing = true; };
    document.getElementById('pause').onclick = function() { playing = false; };
    drawFrame(0);
    requestAnimationFrame(tick);
  </script>
</body>
</html>
"#;
    TEMPLATE.replace("__STATE_JSON__", &escaped)
}
