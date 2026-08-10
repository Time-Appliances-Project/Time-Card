//! Terminal interface for the Linux Time Card telemetry backend.

mod cli;
mod render;
mod runtime;
mod state;

pub use cli::{TuiConfig, TuiPage};
pub use runtime::{TuiError, run};
