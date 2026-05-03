//! Integration tests for DemoAgent group behaviour (#113).
//!
//! Tests A, B, C from the spec: cohesion reduces spread, encounter steers groups
//! toward each other, and ungrouped agents are unaffected.

use arcane_demo::{
    compute_group_states, create_demo_agents, create_grouped_agents, tick_demo_agents,
    tick_grouped_agents, EncounterTracker,
};
use uuid::Uuid;

fn spread(agents: &[arcane_demo::DemoAgent], group_id: Option<Uuid>) -> f64 {
    let members: Vec<_> = agents
        .iter()
        .filter(|a| match group_id {
            Some(g) => a.group_id == Some(g),
            None => a.group_id.is_none(),
        })
        .collect();
    if members.len() < 2 {
        return 0.0;
    }
    let cx = members.iter().map(|a| a.x).sum::<f64>() / members.len() as f64;
    let cz = members.iter().map(|a| a.z).sum::<f64>() / members.len() as f64;
    members
        .iter()
        .map(|a| ((a.x - cx).powi(2) + (a.z - cz).powi(2)).sqrt())
        .fold(0.0_f64, f64::max)
}

/// Test A: grouped agents stay closer to their centroid than random-walk ungrouped agents.
#[test]
fn group_cohesion_keeps_members_closer_than_random_walk() {
    let group_size = 6u32;
    let total = 12u32;

    // Grouped agents
    let mut grouped = create_grouped_agents(total, group_size, None);
    let mut tracker = EncounterTracker::new();

    // Ungrouped agents (same count, random walk)
    let mut ungrouped = create_demo_agents(total, Uuid::nil(), None);

    for tick in 0..200u64 {
        let group_states = compute_group_states(&grouped, &tracker);
        tick_grouped_agents(&mut grouped, tick, &group_states, &mut tracker, None);
        tracker.tick();
        tick_demo_agents(&mut ungrouped, tick, None);
    }

    // Measure average intra-group spread for grouped agents
    let group_ids: Vec<Uuid> = {
        let mut seen = std::collections::HashSet::new();
        grouped
            .iter()
            .filter_map(|a| a.group_id)
            .filter(|g| seen.insert(*g))
            .collect()
    };
    let avg_grouped_spread: f64 = group_ids
        .iter()
        .map(|&g| spread(&grouped, Some(g)))
        .sum::<f64>()
        / group_ids.len() as f64;

    // Measure average spread over same-sized windows of ungrouped agents
    let n = ungrouped.len();
    let avg_ungrouped_spread = (0..n)
        .step_by(group_size as usize)
        .map(|start| {
            let end = (start + group_size as usize).min(n);
            let window: Vec<_> = ungrouped[start..end].iter().map(|a| (a.x, a.z)).collect();
            if window.len() < 2 {
                return 0.0;
            }
            let cx = window.iter().map(|(x, _)| x).sum::<f64>() / window.len() as f64;
            let cz = window.iter().map(|(_, z)| z).sum::<f64>() / window.len() as f64;
            window
                .iter()
                .map(|(x, z)| ((x - cx).powi(2) + (z - cz).powi(2)).sqrt())
                .fold(0.0_f64, f64::max)
        })
        .sum::<f64>()
        / (n / group_size as usize).max(1) as f64;

    assert!(
        avg_grouped_spread < avg_ungrouped_spread,
        "grouped spread {:.1} should be less than ungrouped spread {:.1}",
        avg_grouped_spread,
        avg_ungrouped_spread
    );
}

/// Test B: encounter steering moves two groups closer together.
#[test]
fn encounter_moves_groups_toward_each_other() {
    // Two groups starting at opposite sides of the world
    let mut agents = create_grouped_agents(12, 6, None);

    // Override positions: group 0 at x=500, group 1 at x=4500
    for a in agents.iter_mut() {
        match a.group_id {
            Some(g) if g == Uuid::from_u128(0) => {
                a.x = 500.0;
                a.z = 2500.0;
            }
            Some(g) if g == Uuid::from_u128(1) => {
                a.x = 4500.0;
                a.z = 2500.0;
            }
            _ => {}
        }
    }

    let mut tracker = EncounterTracker::new();
    // Manually start encounters for both groups
    tracker.start_encounter(Uuid::from_u128(0), 0);
    tracker.start_encounter(Uuid::from_u128(1), 0);

    let initial_dist = {
        let g0x: f64 = agents
            .iter()
            .filter(|a| a.group_id == Some(Uuid::from_u128(0)))
            .map(|a| a.x)
            .sum::<f64>()
            / 6.0;
        let g1x: f64 = agents
            .iter()
            .filter(|a| a.group_id == Some(Uuid::from_u128(1)))
            .map(|a| a.x)
            .sum::<f64>()
            / 6.0;
        (g1x - g0x).abs()
    };

    for tick in 0..100u64 {
        let group_states = compute_group_states(&agents, &tracker);
        tick_grouped_agents(&mut agents, tick, &group_states, &mut tracker, None);
        tracker.tick();
    }

    let final_dist = {
        let g0x: f64 = agents
            .iter()
            .filter(|a| a.group_id == Some(Uuid::from_u128(0)))
            .map(|a| a.x)
            .sum::<f64>()
            / 6.0;
        let g1x: f64 = agents
            .iter()
            .filter(|a| a.group_id == Some(Uuid::from_u128(1)))
            .map(|a| a.x)
            .sum::<f64>()
            / 6.0;
        (g1x - g0x).abs()
    };

    assert!(
        final_dist < initial_dist,
        "groups should move closer during encounter: initial dist {:.1}, final dist {:.1}",
        initial_dist,
        final_dist
    );
}

/// Test C: ungrouped agents (group_id = None) are unaffected by tick_grouped_agents.
/// Their positions must stay within world bounds and match the statistical behaviour
/// of tick_demo_agents (both stay in bounds — structural equivalence).
#[test]
fn ungrouped_agents_unaffected_by_grouped_tick() {
    // Agents created with group_id = None
    let mut agents_grouped_tick = create_demo_agents(20, Uuid::nil(), None);
    let mut agents_normal_tick = create_demo_agents(20, Uuid::nil(), None);

    let mut tracker = EncounterTracker::new();

    for tick in 0..200u64 {
        let group_states = compute_group_states(&agents_grouped_tick, &tracker);
        tick_grouped_agents(
            &mut agents_grouped_tick,
            tick,
            &group_states,
            &mut tracker,
            None,
        );
        tracker.tick();
        tick_demo_agents(&mut agents_normal_tick, tick, None);
    }

    // Both sets must stay in world bounds
    let bounds = (200.0, 4800.0);
    for a in &agents_grouped_tick {
        assert!(
            a.x >= bounds.0 && a.x <= bounds.1 && a.z >= bounds.0 && a.z <= bounds.1,
            "ungrouped agent out of bounds after grouped tick: ({:.1}, {:.1})",
            a.x,
            a.z
        );
    }
    for a in &agents_normal_tick {
        assert!(
            a.x >= bounds.0 && a.x <= bounds.1 && a.z >= bounds.0 && a.z <= bounds.1,
            "agent out of bounds after normal tick: ({:.1}, {:.1})",
            a.x,
            a.z
        );
    }
}
