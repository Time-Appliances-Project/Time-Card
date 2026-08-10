//! Native Linux telemetry and user-interface support for the OCP Time Card.

pub mod backend;
pub mod config;
pub mod linux;
pub mod mock;
pub mod oscillatord;
pub mod session_log;
pub mod snapshot;
pub mod timing;

#[cfg(feature = "gui")]
pub mod app;

#[cfg(feature = "tui")]
pub mod tui;

pub use backend::TimeCardBackend;
pub use config::{AppConfig, Page};
pub use linux::LinuxTimeCardBackend;
pub use mock::MockTimeCardBackend;
pub use snapshot::TimeCardSnapshot;
