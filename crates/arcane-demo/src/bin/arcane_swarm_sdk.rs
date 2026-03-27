//! Headless client swarm for SpacetimeDB benchmarking.
//!
//! Each simulated player is an "Unreal stub" — identical wire protocol to a real
//! Unreal client (WebSocket, BSATN, subscriptions), just without rendering.
//!
//! Build:
//!   cargo build -p arcane-demo --bin arcane-swarm-sdk --features swarm-sdk --release
//!
//! Usage:
//!   arcane-swarm-sdk --players 200 --mode spread --duration 60
//!   arcane-swarm-sdk --players 200 --mode clustered --duration 60
//!   arcane-swarm-sdk --players 100 --tick-rate 30 --aps 4 --duration 120

#[cfg(feature = "swarm-sdk")]
fn main() {
    arcane_demo::swarm::run();
}

#[cfg(not(feature = "swarm-sdk"))]
fn main() {
    eprintln!("This binary requires the `swarm-sdk` feature.");
    eprintln!("Build with: cargo build -p arcane-demo --bin arcane-swarm-sdk --features swarm-sdk");
    std::process::exit(1);
}
