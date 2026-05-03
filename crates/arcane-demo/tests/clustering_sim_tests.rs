//! Integration test — Test E: affinity model outperforms rules on group scenario.
//!
//! 30 agents, 3 groups of 10, 3 initial zones (round-robin agent assignment so
//! each group starts split across all zones — fragmentation = 1.0 initially).
//!
//! AffinityEngine receives party signals via PlayerInfo.party_id and pulls group
//! members into the same desired cluster. RulesEngine has no party concept and
//! leaves all groups fragmented.

#[cfg(feature = "clustering-sim")]
mod tests {
    use arcane_demo::{run_sim, Model, SimConfig};

    #[test]
    fn affinity_outperforms_rules_on_group_scenario() {
        let base = SimConfig {
            total_agents: 30,
            group_size: 10,
            zones: 3,
            ticks: 300,
            eval_interval: 10,
            model: Model::Rules,
        };

        let rules = run_sim(&SimConfig {
            model: Model::Rules,
            ..base
        });
        let affinity = run_sim(&SimConfig {
            model: Model::Affinity,
            ..base
        });

        // AffinityEngine should co-locate all groups within the first evaluation window
        assert!(
            affinity.final_fragmentation < 0.1,
            "affinity model should co-locate all groups: got fragmentation {:.4}",
            affinity.final_fragmentation
        );

        // RulesEngine has no party signal — fragmentation should stay high
        assert!(
            rules.final_fragmentation > 0.9,
            "rules model should leave groups fragmented: got fragmentation {:.4}",
            rules.final_fragmentation
        );

        // The gap should be substantial at every checkpoint from tick 10 onward
        for snap in &affinity.snapshots {
            let rules_snap = rules
                .snapshots
                .iter()
                .find(|s| s.tick == snap.tick)
                .unwrap();
            assert!(
                snap.group_fragmentation < rules_snap.group_fragmentation,
                "affinity ({:.4}) should be below rules ({:.4}) at tick {}",
                snap.group_fragmentation,
                rules_snap.group_fragmentation,
                snap.tick
            );
        }
    }
}
