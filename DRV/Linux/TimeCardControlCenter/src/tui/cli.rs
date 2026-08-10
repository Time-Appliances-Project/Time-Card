use std::path::PathBuf;

use clap::{Parser, ValueEnum};

/// Runtime configuration for the terminal interface.
#[derive(Clone, Debug, Parser)]
#[command(
    name = "timecard-control-center-tui",
    version,
    about = "Linux terminal telemetry center for the OCP Time Card"
)]
pub struct TuiConfig {
    /// Use the built-in hardware simulation.
    #[arg(long)]
    pub mock: bool,

    /// Override /sys/class/timecard for fixtures or testing.
    #[arg(
        long,
        env = "TIMECARD_SYSFS_ROOT",
        default_value = "/sys/class/timecard"
    )]
    pub sysfs_root: PathBuf,

    /// Override /sys/class/hwmon for fixtures or testing.
    #[arg(long, env = "TIMECARD_HWMON_ROOT", default_value = "/sys/class/hwmon")]
    pub hwmon_root: PathBuf,

    /// Override /sys/bus/iio/devices for fixtures or testing.
    #[arg(
        long,
        env = "TIMECARD_IIO_ROOT",
        default_value = "/sys/bus/iio/devices"
    )]
    pub iio_root: PathBuf,

    /// Override /sys/class/leds for fixtures or testing.
    #[arg(long, env = "TIMECARD_LEDS_ROOT", default_value = "/sys/class/leds")]
    pub leds_root: PathBuf,

    /// oscillatord monitoring host.
    #[arg(long, env = "TIMECARD_OSCILLATORD_HOST", default_value = "127.0.0.1")]
    pub oscillatord_host: String,

    /// oscillatord monitoring port.
    #[arg(
        long,
        env = "TIMECARD_OSCILLATORD_PORT",
        default_value_t = 2958,
        value_parser = clap::value_parser!(u16).range(1..)
    )]
    pub oscillatord_port: u16,

    /// Workspace to show when the interface opens.
    #[arg(long, value_enum, default_value_t = TuiPage::Overview)]
    pub page: TuiPage,

    /// Print one complete, ANSI-free snapshot and exit.
    #[arg(long)]
    pub plain: bool,

    /// Exit after this many milliseconds, useful for smoke tests.
    #[arg(long)]
    pub quit_after: Option<u64>,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq, ValueEnum)]
pub enum TuiPage {
    #[default]
    Overview,
    #[value(alias = "timing", alias = "io", alias = "fpga")]
    TimingIo,
    Sensors,
    Gnss,
    #[value(alias = "oscillator")]
    Oscillatord,
    Help,
}

impl TuiPage {
    pub const ALL: [Self; 6] = [
        Self::Overview,
        Self::TimingIo,
        Self::Sensors,
        Self::Gnss,
        Self::Oscillatord,
        Self::Help,
    ];

    #[must_use]
    pub const fn title(self) -> &'static str {
        match self {
            Self::Overview => "Overview",
            Self::TimingIo => "Timing I/O",
            Self::Sensors => "Sensors",
            Self::Gnss => "GNSS",
            Self::Oscillatord => "oscillatord",
            Self::Help => "Help",
        }
    }

    #[must_use]
    pub const fn number(self) -> Option<u8> {
        match self {
            Self::Overview => Some(1),
            Self::TimingIo => Some(2),
            Self::Sensors => Some(3),
            Self::Gnss => Some(4),
            Self::Oscillatord => Some(5),
            Self::Help => None,
        }
    }

    #[must_use]
    pub fn next(self) -> Self {
        let index = Self::ALL.iter().position(|page| *page == self).unwrap_or(0);
        Self::ALL[(index + 1) % Self::ALL.len()]
    }

    #[must_use]
    pub fn previous(self) -> Self {
        let index = Self::ALL.iter().position(|page| *page == self).unwrap_or(0);
        Self::ALL[(index + Self::ALL.len() - 1) % Self::ALL.len()]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn page_navigation_wraps() {
        assert_eq!(TuiPage::Help.next(), TuiPage::Overview);
        assert_eq!(TuiPage::Overview.previous(), TuiPage::Help);
    }
}
