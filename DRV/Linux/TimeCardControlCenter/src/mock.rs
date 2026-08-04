use std::time::{Instant, SystemTime, UNIX_EPOCH};

use crate::backend::TimeCardBackend;
use crate::snapshot::TimeCardSnapshot;
use crate::timing::NANOSECONDS_PER_SECOND;

#[derive(Debug)]
pub struct MockTimeCardBackend {
    started: Instant,
    selected_device: String,
}

impl MockTimeCardBackend {
    #[must_use]
    pub fn new() -> Self {
        Self {
            started: Instant::now(),
            selected_device: "mock0".to_owned(),
        }
    }
}

impl Default for MockTimeCardBackend {
    fn default() -> Self {
        Self::new()
    }
}

impl TimeCardBackend for MockTimeCardBackend {
    fn backend_name(&self) -> &'static str {
        "Recorded hardware simulation"
    }

    fn selected_device(&self) -> &str {
        &self.selected_device
    }

    fn set_selected_device(&mut self, device_id: &str) {
        if self
            .available_devices()
            .iter()
            .any(|device| device == device_id)
        {
            self.selected_device = device_id.to_owned();
        }
    }

    fn available_devices(&self) -> Vec<String> {
        vec!["mock0".to_owned(), "mock1".to_owned()]
    }

    fn read_snapshot(&mut self) -> TimeCardSnapshot {
        let elapsed = self.started.elapsed().as_secs_f64();
        let system_utc = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_or(0, |duration| {
                duration.as_nanos().min(i64::MAX as u128) as i64
            });
        let secondary = self.selected_device == "mock1";
        let phase = if secondary { 1.3 } else { 0.0 };
        let offset = (42.0 * (elapsed / 8.0 + phase).sin() + 8.0 * (elapsed / 2.1).sin()) as i64;
        let tai_offset_seconds = 37;
        let phc_tai = system_utc + tai_offset_seconds * NANOSECONDS_PER_SECOND + offset;

        TimeCardSnapshot {
            connected: true,
            backend_name: self.backend_name().to_owned(),
            device_id: self.selected_device.clone(),
            sysfs_path: format!("/sys/class/timecard/{}", self.selected_device),
            ptp_device: if secondary { "/dev/ptp1" } else { "/dev/ptp0" }.to_owned(),
            pps_device: if secondary { "/dev/pps1" } else { "/dev/pps0" }.to_owned(),
            i2c_device: if secondary {
                "/dev/i2c-8"
            } else {
                "/dev/i2c-4"
            }
            .to_owned(),
            pci_address: if secondary {
                "0000:04:00.0"
            } else {
                "0000:03:00.0"
            }
            .to_owned(),
            pci_vendor: "0x1d9b".to_owned(),
            pci_device: "0x0400".to_owned(),
            serial_number: if secondary {
                "02:54:43:00:00:02"
            } else {
                "02:54:43:00:00:01"
            }
            .to_owned(),
            board_profile: "R4006-compatible peripheral profile".to_owned(),
            clock_source: "PPS".to_owned(),
            gnss_state: "SYNC".to_owned(),
            gnss_locked: true,
            tod_protocol: "NMEA".to_owned(),
            tod_baud_rate: "115200".to_owned(),
            tty_gnss: if secondary {
                "/dev/ttyS8"
            } else {
                "/dev/ttyS4"
            }
            .to_owned(),
            tty_gnss2: if secondary {
                "/dev/ttyS9"
            } else {
                "/dev/ttyS5"
            }
            .to_owned(),
            tty_mac: if secondary {
                "/dev/ttyS10"
            } else {
                "/dev/ttyS6"
            }
            .to_owned(),
            tty_nmea: if secondary {
                "/dev/ttyS11"
            } else {
                "/dev/ttyS7"
            }
            .to_owned(),
            card_utc_tai_offset: Some(tai_offset_seconds as i32),
            utc_tai_offset: Some(tai_offset_seconds as i32),
            clock_drift_ppb: Some((2.0 * (elapsed / 5.0).sin()) as i64),
            r4006_topology_detected: true,
            clock_offset_nanoseconds: Some(offset),
            phc_tai_nanoseconds: Some(phc_tai),
            phc_utc_nanoseconds: Some(phc_tai - tai_offset_seconds * NANOSECONDS_PER_SECOND),
            system_utc_nanoseconds: system_utc,
            offset_nanoseconds: Some(offset),
            sample_window_nanoseconds: Some(760 + (120.0 * (1.0 + (elapsed / 3.0).sin())) as i64),
            timestamp_method: "PTP_SYS_OFFSET_EXTENDED simulation".to_owned(),
            available_devices: self.available_devices(),
            capabilities: [
                "PHC",
                "GNSS",
                "UART",
                "SMA",
                "Signal generators",
                "Frequency counters",
                "I2C",
                "SPI flash configuration",
                "FPGA engine status",
                "Sensors",
                "Status LEDs",
            ]
            .into_iter()
            .map(str::to_owned)
            .collect(),
            sma_states: [
                "SMA1 | OUT: PHC PPS",
                "SMA2 | IN: PPS1",
                "SMA3 | OUT: GNSS1",
                "SMA4 | disabled",
            ]
            .into_iter()
            .map(str::to_owned)
            .collect(),
            generator_states: [
                "GEN1 | running | period 1000000000 ns | duty 50% | active high | continuous",
                "GEN2 | stopped | period 10000000 ns | duty 25% | active high | repeat 100",
            ]
            .into_iter()
            .map(str::to_owned)
            .collect(),
            frequency_counter_states: [
                "FREQ1 | 10000000 Hz | gate 1 s",
                "FREQ2 | waiting for sample | gate 1 s",
            ]
            .into_iter()
            .map(str::to_owned)
            .collect(),
            fpga_engine_states: [
                "PPS | external polarity 1 | pulse width ms 100 | external cable ns 0",
                "NMEA output | enabled 1 | baud 115200 | GNSS COMBINED | errors 0",
                "ToD parser | protocol NMEA | GNSS COMBINED | baud 115200 | errors 0",
                "IRIG/DCF | output mode B | input mode B | output error 0 | input error 0",
            ]
            .into_iter()
            .map(str::to_owned)
            .collect(),
            sensor_states: [
                "LM75B 0x48 | 41.25 C",
                "LM75B 0x49 | 42.00 C",
                "LM75B 0x4a | 40.88 C",
                "SHT3x 0x44 | 39.75 C",
                "SHT3x 0x44 humidity | 44.20%",
                "ICP-10100 pressure | 101.325 kPa",
            ]
            .into_iter()
            .map(str::to_owned)
            .collect(),
            led_states: [
                "GNSS1 | brightness 128/255 | RGB 0 255 0",
                "SMA1 | brightness 96/255 | RGB 0 255 0",
                "SMA2 | brightness 96/255 | RGB 0 0 255",
            ]
            .into_iter()
            .map(str::to_owned)
            .collect(),
            optional_image_contract: format!(
                "pci={} actual=0x12345678 expected=0x12345678 targeted=1 match=1 loader=0",
                if secondary {
                    "0000:04:00.0"
                } else {
                    "0000:03:00.0"
                }
            ),
            ..TimeCardSnapshot::default()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn supports_multiple_simulated_cards() {
        let mut backend = MockTimeCardBackend::new();
        backend.set_selected_device("mock1");
        let snapshot = backend.read_snapshot();
        assert_eq!(snapshot.device_id, "mock1");
        assert_eq!(snapshot.ptp_device, "/dev/ptp1");
        assert!(snapshot.offset_nanoseconds.is_some());
    }

    #[test]
    fn ignores_unknown_selection() {
        let mut backend = MockTimeCardBackend::new();
        backend.set_selected_device("missing");
        assert_eq!(backend.selected_device(), "mock0");
    }
}
