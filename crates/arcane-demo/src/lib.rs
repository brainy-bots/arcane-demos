//! Arcane Engine — demo and visualization (non-library).
//! Keeps game/demo logic (e.g. cluster demo agents with gravity, jump) separate from the library (arcane-infra, arcane-core).

pub mod cluster_demo;

pub use cluster_demo::{agents_to_entries, create_demo_agents, tick_demo_agents, DemoAgent};

#[cfg(feature = "swarm-sdk")]
#[path = "module_bindings/mod.rs"]
pub mod module_bindings;

#[cfg(feature = "swarm-sdk")]
pub mod swarm;
