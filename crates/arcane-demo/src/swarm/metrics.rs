//! Lock-free atomic metrics for the load-testing swarm.
//!
//! All counters use `Relaxed` ordering — we tolerate slight imprecision in
//! exchange for zero contention across hundreds of player threads.
//!
//! For latency percentiles we keep a lock-free histogram of buckets.

use std::sync::atomic::{AtomicU64, Ordering};
use std::time::Duration;

/// Histogram bucket boundaries in microseconds.
/// Chosen to cover the 0.1 ms → 30+ s range with useful resolution.
const BUCKET_BOUNDS_US: [u64; 16] = [
    100,        // 0.1 ms
    500,        // 0.5 ms
    1_000,      // 1 ms
    2_000,      // 2 ms
    5_000,      // 5 ms
    10_000,     // 10 ms
    20_000,     // 20 ms
    50_000,     // 50 ms
    100_000,    // 100 ms
    200_000,    // 200 ms
    500_000,    // 500 ms
    1_000_000,  // 1 s
    2_000_000,  // 2 s
    5_000_000,  // 5 s
    10_000_000, // 10 s
    30_000_000, // 30 s
];

const NUM_BUCKETS: usize = BUCKET_BOUNDS_US.len() + 1; // last bucket = overflow

fn bucket_for(us: u64) -> usize {
    BUCKET_BOUNDS_US.iter().position(|&b| us < b).unwrap_or(BUCKET_BOUNDS_US.len())
}

/// Shared counters written by every player thread, read by the reporter.
pub struct Metrics {
    pub reducer_calls: AtomicU64,
    pub reducer_oks: AtomicU64,
    pub reducer_errs: AtomicU64,
    pub sub_updates: AtomicU64,
    pub sub_inserts: AtomicU64,
    pub sub_deletes: AtomicU64,
    latency_sum_us: AtomicU64,
    latency_samples: AtomicU64,
    latency_max_us: AtomicU64,
    latency_buckets: [AtomicU64; NUM_BUCKETS],
}

/// A point-in-time snapshot produced by [`Metrics::snapshot_and_reset`].
pub struct Snapshot {
    pub calls: u64,
    pub oks: u64,
    pub errs: u64,
    pub sub_updates: u64,
    pub sub_inserts: u64,
    pub sub_deletes: u64,
    pub avg_latency_us: u64,
    pub max_latency_us: u64,
    pub p50_latency_us: u64,
    pub p95_latency_us: u64,
    pub p99_latency_us: u64,
}

impl Metrics {
    pub fn new() -> Self {
        const ZERO: AtomicU64 = AtomicU64::new(0);
        Self {
            reducer_calls: AtomicU64::new(0),
            reducer_oks: AtomicU64::new(0),
            reducer_errs: AtomicU64::new(0),
            sub_updates: AtomicU64::new(0),
            sub_inserts: AtomicU64::new(0),
            sub_deletes: AtomicU64::new(0),
            latency_sum_us: AtomicU64::new(0),
            latency_samples: AtomicU64::new(0),
            latency_max_us: AtomicU64::new(0),
            latency_buckets: [ZERO; NUM_BUCKETS],
        }
    }

    /// Record a successful reducer round-trip.
    pub fn record_reducer_ok(&self, latency: Duration) {
        self.reducer_oks.fetch_add(1, Ordering::Relaxed);
        let us = latency.as_micros() as u64;
        self.latency_sum_us.fetch_add(us, Ordering::Relaxed);
        self.latency_samples.fetch_add(1, Ordering::Relaxed);
        self.latency_max_us.fetch_max(us, Ordering::Relaxed);
        self.latency_buckets[bucket_for(us)].fetch_add(1, Ordering::Relaxed);
    }

    pub fn record_reducer_err(&self) {
        self.reducer_errs.fetch_add(1, Ordering::Relaxed);
    }

    pub fn record_call(&self) {
        self.reducer_calls.fetch_add(1, Ordering::Relaxed);
    }

    /// Atomically read and zero all counters, returning the accumulated values.
    pub fn snapshot_and_reset(&self) -> Snapshot {
        let sum = self.latency_sum_us.swap(0, Ordering::Relaxed);
        let n = self.latency_samples.swap(0, Ordering::Relaxed);

        let mut buckets = [0u64; NUM_BUCKETS];
        for (i, b) in self.latency_buckets.iter().enumerate() {
            buckets[i] = b.swap(0, Ordering::Relaxed);
        }

        Snapshot {
            calls: self.reducer_calls.swap(0, Ordering::Relaxed),
            oks: self.reducer_oks.swap(0, Ordering::Relaxed),
            errs: self.reducer_errs.swap(0, Ordering::Relaxed),
            sub_updates: self.sub_updates.swap(0, Ordering::Relaxed),
            sub_inserts: self.sub_inserts.swap(0, Ordering::Relaxed),
            sub_deletes: self.sub_deletes.swap(0, Ordering::Relaxed),
            avg_latency_us: if n > 0 { sum / n } else { 0 },
            max_latency_us: self.latency_max_us.swap(0, Ordering::Relaxed),
            p50_latency_us: percentile_from_buckets(&buckets, n, 0.50),
            p95_latency_us: percentile_from_buckets(&buckets, n, 0.95),
            p99_latency_us: percentile_from_buckets(&buckets, n, 0.99),
        }
    }
}

/// Estimate a percentile from the histogram bucket counts.
fn percentile_from_buckets(buckets: &[u64; NUM_BUCKETS], total: u64, pct: f64) -> u64 {
    if total == 0 {
        return 0;
    }
    let target = (total as f64 * pct).ceil() as u64;
    let mut cumulative = 0u64;
    for (i, &count) in buckets.iter().enumerate() {
        cumulative += count;
        if cumulative >= target {
            return if i < BUCKET_BOUNDS_US.len() {
                BUCKET_BOUNDS_US[i]
            } else {
                BUCKET_BOUNDS_US[BUCKET_BOUNDS_US.len() - 1] * 2 // overflow bucket
            };
        }
    }
    0
}

impl Snapshot {
    pub fn sub_total(&self) -> u64 {
        self.sub_updates + self.sub_inserts + self.sub_deletes
    }

    pub fn avg_latency_ms(&self) -> f64 {
        self.avg_latency_us as f64 / 1000.0
    }

    pub fn max_latency_ms(&self) -> f64 {
        self.max_latency_us as f64 / 1000.0
    }

    pub fn p50_latency_ms(&self) -> f64 {
        self.p50_latency_us as f64 / 1000.0
    }

    pub fn p95_latency_ms(&self) -> f64 {
        self.p95_latency_us as f64 / 1000.0
    }

    pub fn p99_latency_ms(&self) -> f64 {
        self.p99_latency_us as f64 / 1000.0
    }
}
