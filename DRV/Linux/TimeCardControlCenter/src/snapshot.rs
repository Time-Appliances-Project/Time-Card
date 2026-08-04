use serde::{Deserialize, Serialize};

/// One internally consistent read of a selected Time Card and its Linux peers.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TimeCardSnapshot {
    pub connected: bool,
    pub backend_name: String,
    pub device_id: String,
    pub sysfs_path: String,
    pub ptp_device: String,
    pub pps_device: String,
    pub i2c_device: String,
    pub mro50_device: String,
    pub pci_address: String,
    pub pci_vendor: String,
    pub pci_device: String,
    pub serial_number: String,
    pub board_profile: String,

    pub clock_source: String,
    pub gnss_state: String,
    pub gnss_locked: bool,
    pub tod_protocol: String,
    pub tod_baud_rate: String,

    pub tty_gnss: String,
    pub tty_gnss2: String,
    pub tty_mac: String,
    pub tty_nmea: String,

    pub card_utc_tai_offset: Option<i32>,
    pub utc_tai_offset: Option<i32>,
    pub utc_tai_offset_from_kernel: bool,
    pub clock_drift_ppb: Option<i64>,
    pub r4006_topology_detected: bool,
    pub clock_offset_nanoseconds: Option<i64>,

    pub phc_tai_nanoseconds: Option<i64>,
    pub phc_utc_nanoseconds: Option<i64>,
    pub system_utc_nanoseconds: i64,
    pub offset_nanoseconds: Option<i64>,
    pub sample_window_nanoseconds: Option<i64>,
    pub timestamp_method: String,

    pub available_devices: Vec<String>,
    pub capabilities: Vec<String>,
    pub sma_states: Vec<String>,
    pub generator_states: Vec<String>,
    pub frequency_counter_states: Vec<String>,
    pub fpga_engine_states: Vec<String>,
    pub sensor_states: Vec<String>,
    pub led_states: Vec<String>,
    pub optional_image_contract: String,
    pub errors: Vec<String>,
}

impl TimeCardSnapshot {
    pub fn push_error(&mut self, message: impl Into<String>) {
        let message = message.into();
        if !message.is_empty() && !self.errors.contains(&message) {
            self.errors.push(message);
        }
    }

    #[must_use]
    pub fn error_text(&self) -> String {
        self.errors.join("\n")
    }
}
