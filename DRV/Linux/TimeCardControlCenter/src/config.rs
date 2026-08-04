use std::path::PathBuf;

use clap::{Parser, ValueEnum};

/// Runtime configuration shared by the GTK application and telemetry worker.
#[derive(Clone, Debug, Parser)]
#[command(
    name = "timecard-control-center",
    version,
    about = "Native Linux control and telemetry dashboard for the OCP Time Card"
)]
pub struct AppConfig {
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

    /// Workspace to show when the application opens.
    #[arg(long, value_enum, default_value_t = Page::Overview)]
    pub page: Page,

    /// Exit after this many milliseconds, useful for smoke tests.
    #[arg(long)]
    pub quit_after: Option<u64>,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq, ValueEnum)]
pub enum Page {
    #[default]
    Overview,
    #[value(alias = "timing", alias = "io", alias = "fpga")]
    TimingIo,
    Sensors,
    Gnss,
    #[value(alias = "oscillator")]
    Oscillatord,
}

impl Page {
    #[must_use]
    pub const fn stack_name(self) -> &'static str {
        match self {
            Self::Overview => "overview",
            Self::TimingIo => "timing-io",
            Self::Sensors => "sensors",
            Self::Gnss => "gnss",
            Self::Oscillatord => "oscillatord",
        }
    }
}
