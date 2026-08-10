use std::process::ExitCode;

use clap::Parser;
use timecard_control_center::tui::{TuiConfig, run};

fn main() -> ExitCode {
    let config = TuiConfig::parse();
    match run(config) {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("timecard-control-center-tui: {error}");
            ExitCode::from(2)
        }
    }
}
