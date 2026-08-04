mod phc;
mod sensors;

use std::fs;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use crate::backend::TimeCardBackend;
use crate::snapshot::TimeCardSnapshot;
use crate::timing::{TaiOffsetSource, derive_tai_aware_timing, select_tai_offset};

#[derive(Debug)]
pub struct LinuxTimeCardBackend {
    sysfs_root: PathBuf,
    hwmon_root: PathBuf,
    iio_root: PathBuf,
    leds_root: PathBuf,
    selected_device: String,
    identity_cache: Option<IdentityCache>,
}

#[derive(Clone, Debug)]
struct IdentityCache {
    device: String,
    captured: Instant,
    snapshot: TimeCardSnapshot,
}

impl LinuxTimeCardBackend {
    #[must_use]
    pub fn new(
        sysfs_root: PathBuf,
        hwmon_root: PathBuf,
        iio_root: PathBuf,
        leds_root: PathBuf,
    ) -> Self {
        Self {
            sysfs_root,
            hwmon_root,
            iio_root,
            leds_root,
            selected_device: String::new(),
            identity_cache: None,
        }
    }

    #[must_use]
    pub fn system_default() -> Self {
        Self::new(
            PathBuf::from("/sys/class/timecard"),
            PathBuf::from("/sys/class/hwmon"),
            PathBuf::from("/sys/bus/iio/devices"),
            PathBuf::from("/sys/class/leds"),
        )
    }

    #[must_use]
    pub fn sysfs_root(&self) -> &Path {
        &self.sysfs_root
    }

    fn card_path(&self, device_id: &str) -> PathBuf {
        self.sysfs_root.join(device_id)
    }

    fn tty_device(card_path: &Path, name: &str) -> String {
        resolve_device_node(&card_path.join("tty").join(name))
            .or_else(|| resolve_device_node(&card_path.join(name)))
            .unwrap_or_default()
    }

    fn read_identity(&self, card: &Path) -> TimeCardSnapshot {
        let mut snapshot = TimeCardSnapshot {
            connected: true,
            backend_name: self.backend_name().to_owned(),
            device_id: self.selected_device.clone(),
            sysfs_path: card.to_string_lossy().into_owned(),
            ptp_device: resolve_device_node(&card.join("ptp")).unwrap_or_default(),
            pps_device: resolve_device_node(&card.join("pps")).unwrap_or_default(),
            i2c_device: resolve_device_node(&card.join("i2c")).unwrap_or_default(),
            mro50_device: resolve_device_node(&card.join("mro50")).unwrap_or_default(),
            serial_number: read_text(&card.join("serialnum")),
            ..TimeCardSnapshot::default()
        };
        snapshot.tty_gnss = Self::tty_device(card, "ttyGNSS");
        snapshot.tty_gnss2 = Self::tty_device(card, "ttyGNSS2");
        snapshot.tty_mac = Self::tty_device(card, "ttyMAC");
        snapshot.tty_nmea = Self::tty_device(card, "ttyNMEA");
        discover_pci_identity(card, &mut snapshot);

        if card.join("ptp").exists() {
            add_unique(&mut snapshot.capabilities, "PHC");
        }
        if card.join("gnss_sync").exists() || !snapshot.tty_gnss.is_empty() {
            add_unique(&mut snapshot.capabilities, "GNSS");
        }
        if card.join("pps").exists() {
            add_unique(&mut snapshot.capabilities, "PPS");
        }
        if [
            &snapshot.tty_gnss,
            &snapshot.tty_gnss2,
            &snapshot.tty_mac,
            &snapshot.tty_nmea,
        ]
        .iter()
        .any(|value| !value.is_empty())
        {
            add_unique(&mut snapshot.capabilities, "UART");
        }
        for (entry, capability) in [
            ("mro50", "mRO-50"),
            ("i2c", "I2C"),
            ("config", "SPI flash configuration"),
        ] {
            if card.join(entry).exists() {
                add_unique(&mut snapshot.capabilities, capability);
            }
        }
        snapshot
    }

    fn identity_snapshot(&mut self, card: &Path) -> TimeCardSnapshot {
        if let Some(cache) = &self.identity_cache {
            let lifetime = if cache.snapshot.ptp_device.is_empty() {
                Duration::from_secs(2)
            } else {
                Duration::from_secs(60)
            };
            if cache.device == self.selected_device && cache.captured.elapsed() < lifetime {
                return cache.snapshot.clone();
            }
        }
        let snapshot = self.read_identity(card);
        self.identity_cache = Some(IdentityCache {
            device: self.selected_device.clone(),
            captured: Instant::now(),
            snapshot: snapshot.clone(),
        });
        snapshot
    }

    fn read_timing_io(&self, snapshot: &mut TimeCardSnapshot) {
        let card = PathBuf::from(&snapshot.sysfs_path);
        for index in 1..=4 {
            if let Some(value) = read_optional_text(&card.join(format!("sma{index}")))
                && !value.is_empty()
            {
                snapshot
                    .sma_states
                    .push(labeled(&format!("SMA{index}"), &compact(&value)));
            }
        }

        for index in 1..=4 {
            let generator = card.join(format!("gen{index}"));
            let mut details = Vec::new();
            if let Some(value) = read_optional_text(&generator.join("running")) {
                details.push(display_boolean(&value, "running", "stopped"));
            }
            if let Some(value) = read_optional_text(&generator.join("period")) {
                details.push(format!("period {value} ns"));
            }
            if let Some(value) = read_optional_text(&generator.join("duty")) {
                details.push(format!("duty {value}%"));
            }
            if let Some(value) = read_optional_text(&generator.join("phase")) {
                details.push(format!("phase {value} ns"));
            }
            if let Some(value) = read_optional_text(&generator.join("polarity")) {
                details.push(if value == "1" {
                    "active high".to_owned()
                } else {
                    "active low".to_owned()
                });
            }
            if let Some(value) = read_optional_text(&generator.join("repeat_count")) {
                details.push(if value == "0" {
                    "continuous".to_owned()
                } else {
                    format!("repeat {value}")
                });
            }
            if let Some(value) = read_optional_text(&generator.join("cable_delay")) {
                details.push(format!("cable {value} ns"));
            }
            if let Some(value) = read_optional_text(&generator.join("start"))
                && !value.is_empty()
            {
                details.push(format!("start {} TAI", compact(&value)));
            }
            if !details.is_empty() {
                snapshot
                    .generator_states
                    .push(labeled(&format!("GEN{index}"), &details.join(" | ")));
            }
        }

        for index in 1..=4 {
            let counter = card.join(format!("freq{index}"));
            let seconds = read_optional_text(&counter.join("seconds"));
            let frequency = read_optional_text(&counter.join("frequency"));
            if seconds.is_none() && frequency.is_none() {
                continue;
            }
            let seconds = seconds.unwrap_or_default();
            let mut frequency = frequency.unwrap_or_default();
            if seconds == "0" {
                frequency = "disabled".to_owned();
            } else if frequency.is_empty() {
                frequency = "waiting for sample".to_owned();
            } else if frequency.parse::<u64>().is_ok() {
                frequency.push_str(" Hz");
            } else if matches!(frequency.as_str(), "error" | "overrun") {
                snapshot.push_error(format!(
                    "Frequency counter FREQ{index} reports {frequency}."
                ));
            }
            let detail = if seconds.is_empty() {
                frequency
            } else {
                format!("{frequency} | gate {seconds} s")
            };
            snapshot
                .frequency_counter_states
                .push(labeled(&format!("FREQ{index}"), &detail));
        }

        if !snapshot.sma_states.is_empty() {
            add_unique(&mut snapshot.capabilities, "SMA");
        }
        if !snapshot.generator_states.is_empty() {
            add_unique(&mut snapshot.capabilities, "Signal generators");
        }
        if !snapshot.frequency_counter_states.is_empty() {
            add_unique(&mut snapshot.capabilities, "Frequency counters");
        }
    }

    fn read_fpga_engines(&self, snapshot: &mut TimeCardSnapshot) {
        let card = PathBuf::from(&snapshot.sysfs_path);
        snapshot.optional_image_contract =
            read_optional_text(&card.join("optional_image_contract")).unwrap_or_default();
        if snapshot.optional_image_contract.contains("targeted=1")
            && !snapshot.optional_image_contract.contains("match=1")
        {
            snapshot.push_error("The optional FPGA image contract is targeted but does not match");
        }

        const PPS: &[(&str, &str)] = &[
            ("external polarity", "external_pps_polarity"),
            ("pulse width ms", "external_pps_pulse_width"),
            ("external cable ns", "external_pps_cable_delay"),
            ("internal polarity", "internal_pps_polarity"),
            ("internal cable ns", "internal_pps_cable_delay"),
        ];
        const NMEA: &[(&str, &str)] = &[
            ("enabled", "nmea_enable"),
            ("baud", "nmea_baud_rate"),
            ("GNSS", "nmea_gnss"),
            ("polarity", "nmea_uart_polarity"),
            ("correction s", "nmea_correction_seconds"),
            ("local offset min", "nmea_local_offset_minutes"),
            ("message mask", "nmea_message_disable_mask"),
            ("errors", "nmea_errors"),
        ];
        const TOD: &[(&str, &str)] = &[
            ("protocol", "tod_protocol"),
            ("GNSS", "tod_gnss"),
            ("baud", "tod_baud_rate"),
            ("polarity", "tod_uart_polarity"),
            ("correction s", "tod_correction"),
            ("message mask", "tod_message_disable_mask"),
            ("errors", "tod_errors"),
        ];
        const IRIG: &[(&str, &str)] = &[
            ("output mode", "irig_output_mode"),
            ("input mode", "irig_input_mode"),
            ("B mode", "irig_b_mode"),
            ("output AM", "irig_output_am"),
            ("input code", "irig_input_code"),
            ("manual year", "irig_input_manual_year"),
            ("input AM", "irig_input_am"),
            ("control bits", "irig_output_control_bits"),
            ("input cable ns", "irig_input_cable_delay"),
            ("DCF air delay ns", "dcf_input_air_delay"),
            ("DCF bit", "dcf_input_bit_position"),
            ("output error", "irig_output_error"),
            ("input error", "irig_input_error"),
            ("DCF output error", "dcf_output_error"),
            ("DCF input error", "dcf_input_error"),
        ];

        for (label, attributes) in [
            ("PPS", PPS),
            ("NMEA output", NMEA),
            ("ToD parser", TOD),
            ("IRIG/DCF", IRIG),
        ] {
            let details = read_engine_attributes(&card, attributes, snapshot);
            if !details.is_empty() {
                snapshot
                    .fpga_engine_states
                    .push(labeled(label, &details.join(" | ")));
            }
        }
        if !snapshot.fpga_engine_states.is_empty() {
            add_unique(&mut snapshot.capabilities, "FPGA engine status");
        }
    }
}

impl Default for LinuxTimeCardBackend {
    fn default() -> Self {
        Self::system_default()
    }
}

impl TimeCardBackend for LinuxTimeCardBackend {
    fn backend_name(&self) -> &'static str {
        "Linux ptp_ocp"
    }

    fn selected_device(&self) -> &str {
        &self.selected_device
    }

    fn set_selected_device(&mut self, device_id: &str) {
        if device_id != self.selected_device
            && self
                .available_devices()
                .iter()
                .any(|device| device == device_id)
        {
            self.selected_device = device_id.to_owned();
            self.identity_cache = None;
        }
    }

    fn available_devices(&self) -> Vec<String> {
        let mut devices: Vec<String> = fs::read_dir(&self.sysfs_root)
            .into_iter()
            .flatten()
            .flatten()
            .filter_map(|entry| entry.file_name().into_string().ok())
            .filter(|name| {
                name.strip_prefix("ocp").is_some_and(|suffix| {
                    !suffix.is_empty() && suffix.chars().all(|c| c.is_ascii_digit())
                })
            })
            .collect();
        devices.sort();
        devices
    }

    fn read_snapshot(&mut self) -> TimeCardSnapshot {
        let available_devices = self.available_devices();
        if available_devices.is_empty() {
            self.selected_device.clear();
            self.identity_cache = None;
            let mut snapshot = TimeCardSnapshot {
                backend_name: self.backend_name().to_owned(),
                system_utc_nanoseconds: phc::system_utc_nanoseconds(),
                ..TimeCardSnapshot::default()
            };
            snapshot.push_error(format!(
                "No ptp_ocp Time Card was found in {}.",
                self.sysfs_root.display()
            ));
            return snapshot;
        }
        if !available_devices
            .iter()
            .any(|device| device == &self.selected_device)
        {
            self.selected_device.clone_from(&available_devices[0]);
            self.identity_cache = None;
        }

        let card = self.card_path(&self.selected_device);
        let mut snapshot = self.identity_snapshot(&card);
        snapshot.available_devices = available_devices;
        snapshot.system_utc_nanoseconds = phc::system_utc_nanoseconds();

        snapshot.clock_source = read_text(&card.join("clock_source"));
        snapshot.gnss_state = read_text(&card.join("gnss_sync"));
        snapshot.gnss_locked = snapshot.gnss_state.starts_with("SYNC");
        snapshot.tod_protocol = read_text(&card.join("tod_protocol"));
        snapshot.tod_baud_rate = read_text(&card.join("tod_baud_rate"));
        snapshot.card_utc_tai_offset = read_integer(&card.join("utc_tai_offset"))
            .and_then(|value| i32::try_from(value).ok())
            .filter(|value| *value >= 0);
        snapshot.clock_drift_ppb = read_integer(&card.join("clock_status_drift"));
        snapshot.clock_offset_nanoseconds = read_integer(&card.join("clock_status_offset"));

        self.read_timing_io(&mut snapshot);
        self.read_fpga_engines(&mut snapshot);
        sensors::read_standard_sensors(&self.hwmon_root, &self.iio_root, &mut snapshot);
        sensors::read_standard_leds(&self.leds_root, &self.sysfs_root, &mut snapshot);
        snapshot.board_profile = infer_board_profile(&snapshot).to_owned();

        let kernel_offset = phc::kernel_tai_offset();
        if let Some(offset) = select_tai_offset(kernel_offset, snapshot.card_utc_tai_offset) {
            snapshot.utc_tai_offset = Some(offset.seconds);
            snapshot.utc_tai_offset_from_kernel = offset.source == TaiOffsetSource::Kernel;
        }

        if snapshot.ptp_device.is_empty() {
            snapshot.push_error("The Time Card does not expose a PTP device link.");
        } else {
            match phc::sample(Path::new(&snapshot.ptp_device)) {
                Ok(sample) => {
                    snapshot.phc_tai_nanoseconds = Some(sample.phc_tai_nanoseconds);
                    snapshot.system_utc_nanoseconds = sample.system_utc_nanoseconds;
                    snapshot.sample_window_nanoseconds = sample.sample_window_nanoseconds;
                    snapshot.timestamp_method = sample.method.to_owned();
                    if let Some(tai_offset) = snapshot.utc_tai_offset {
                        if let Some((phc_utc, offset)) = derive_tai_aware_timing(
                            sample.phc_tai_nanoseconds,
                            sample.system_utc_nanoseconds,
                            tai_offset,
                        ) {
                            snapshot.phc_utc_nanoseconds = Some(phc_utc);
                            snapshot.offset_nanoseconds = Some(offset);
                        }
                    } else {
                        snapshot.push_error(
                            "UTC-TAI offset is unavailable; the PHC offset is intentionally not calculated.",
                        );
                    }
                }
                Err(error) => {
                    snapshot.push_error(format!("Cannot sample {}: {error}", snapshot.ptp_device))
                }
            }
        }

        if snapshot.utc_tai_offset_from_kernel
            && snapshot.card_utc_tai_offset.is_some_and(|value| value > 0)
            && snapshot.card_utc_tai_offset != snapshot.utc_tai_offset
        {
            snapshot.push_error(format!(
                "Kernel UTC-TAI offset ({} s) differs from the Time Card attribute ({} s); the kernel value is used for the PHC comparison.",
                snapshot.utc_tai_offset.unwrap_or_default(),
                snapshot.card_utc_tai_offset.unwrap_or_default()
            ));
        }
        snapshot
    }
}

fn read_engine_attributes(
    card: &Path,
    attributes: &[(&str, &str)],
    snapshot: &mut TimeCardSnapshot,
) -> Vec<String> {
    let mut details = Vec::new();
    for (label, attribute) in attributes {
        if let Some(value) = read_optional_text(&card.join(attribute)) {
            let mut display = compact(&value);
            if matches!(*attribute, "nmea_uart_polarity" | "tod_uart_polarity") {
                display = display_boolean(&display, "normal", "inverted");
            }
            details.push(format!("{label} {display}"));
            if (attribute.ends_with("error") || attribute.ends_with("errors"))
                && parse_integer(&value).is_some_and(|errors| errors != 0)
            {
                snapshot.push_error(format!("FPGA {attribute} reports {display}"));
            }
        }
    }
    details
}

fn discover_pci_identity(card_path: &Path, snapshot: &mut TimeCardSnapshot) {
    let current = fs::canonicalize(card_path).unwrap_or_else(|_| card_path.to_path_buf());
    for directory in current.ancestors() {
        if directory.join("vendor").exists() && directory.join("device").exists() {
            snapshot.pci_address = directory
                .file_name()
                .and_then(|name| name.to_str())
                .unwrap_or_default()
                .to_owned();
            snapshot.pci_vendor = read_text(&directory.join("vendor"));
            snapshot.pci_device = read_text(&directory.join("device"));
            return;
        }
    }
}

fn infer_board_profile(snapshot: &TimeCardSnapshot) -> &'static str {
    if snapshot.pci_vendor.eq_ignore_ascii_case("0x1ad7")
        && snapshot.pci_device.eq_ignore_ascii_case("0xa000")
    {
        "Orolia/Safran ART"
    } else if snapshot.r4006_topology_detected {
        "R4006-compatible peripheral profile"
    } else if snapshot.pci_vendor.eq_ignore_ascii_case("0x18d4") {
        "Celestica Time Card"
    } else if snapshot.pci_vendor.eq_ignore_ascii_case("0x1d9b") {
        "OCP Time Card"
    } else {
        "Time Card"
    }
}

pub(crate) fn read_text(path: &Path) -> String {
    read_optional_text(path).unwrap_or_default()
}

pub(crate) fn read_optional_text(path: &Path) -> Option<String> {
    let contents = fs::read(path).ok()?;
    let bounded = &contents[..contents.len().min(4096)];
    Some(String::from_utf8_lossy(bounded).trim().to_owned())
}

pub(crate) fn read_integer(path: &Path) -> Option<i64> {
    parse_integer(&read_text(path))
}

pub(crate) fn parse_integer(value: &str) -> Option<i64> {
    let value = value.trim();
    let (negative, unsigned) = value
        .strip_prefix('-')
        .map_or((false, value), |value| (true, value));
    let magnitude = unsigned
        .strip_prefix("0x")
        .or_else(|| unsigned.strip_prefix("0X"))
        .map_or_else(
            || unsigned.parse::<u64>().ok(),
            |hex| u64::from_str_radix(hex, 16).ok(),
        )?;
    if negative {
        i64::try_from(magnitude).ok()?.checked_neg()
    } else {
        i64::try_from(magnitude).ok()
    }
}

pub(crate) fn compact(value: &str) -> String {
    value
        .lines()
        .map(str::trim)
        .filter(|line| !line.is_empty())
        .collect::<Vec<_>>()
        .join("; ")
}

pub(crate) fn labeled(label: &str, value: &str) -> String {
    format!("{label} | {value}")
}

fn display_boolean(value: &str, on: &str, off: &str) -> String {
    match value {
        "1" => on.to_owned(),
        "0" => off.to_owned(),
        value => value.to_owned(),
    }
}

pub(crate) fn add_unique(values: &mut Vec<String>, value: &str) {
    if !values.iter().any(|existing| existing == value) {
        values.push(value.to_owned());
    }
}

pub(crate) fn resolve_device_node(entry_path: &Path) -> Option<String> {
    let resource = entry_path.file_name()?.to_str()?;
    let candidate = fs::read_link(entry_path)
        .ok()
        .and_then(|target| target.file_name().map(std::ffi::OsStr::to_owned))
        .or_else(|| {
            read_optional_text(entry_path)
                .filter(|value| !value.is_empty())
                .and_then(|value| Path::new(&value).file_name().map(std::ffi::OsStr::to_owned))
        })?;
    let name = candidate.to_str()?;
    valid_device_name(name, resource).then(|| format!("/dev/{name}"))
}

fn valid_device_name(name: &str, resource: &str) -> bool {
    let suffix_is_digits = |prefix: &str| {
        name.strip_prefix(prefix)
            .is_some_and(|suffix| !suffix.is_empty() && suffix.chars().all(|c| c.is_ascii_digit()))
    };
    match resource {
        "ptp" => suffix_is_digits("ptp"),
        "pps" => suffix_is_digits("pps"),
        "i2c" => suffix_is_digits("i2c-"),
        "mro50" => suffix_is_digits("mro50."),
        resource if resource.starts_with("tty") => {
            name.starts_with("tty")
                && name.len() > 3
                && name
                    .chars()
                    .all(|character| character.is_ascii_alphanumeric() || "_.-".contains(character))
        }
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use std::os::unix::fs::symlink;

    use tempfile::TempDir;

    use super::*;

    fn backend(root: &TempDir) -> LinuxTimeCardBackend {
        LinuxTimeCardBackend::new(
            root.path().join("timecard"),
            root.path().join("hwmon"),
            root.path().join("iio"),
            root.path().join("leds"),
        )
    }

    #[test]
    fn discovery_accepts_only_ocp_followed_by_digits() {
        let root = TempDir::new().unwrap();
        let cards = root.path().join("timecard");
        for name in ["ocp0", "ocp12", "ocp", "ocpx", "other0"] {
            fs::create_dir_all(cards.join(name)).unwrap();
        }
        assert_eq!(backend(&root).available_devices(), ["ocp0", "ocp12"]);
    }

    #[test]
    fn node_resolution_rejects_traversal_and_wrong_resource_type() {
        let root = TempDir::new().unwrap();
        fs::write(root.path().join("ptp"), "/dev/../null\n").unwrap();
        fs::write(root.path().join("pps"), "/dev/ptp7\n").unwrap();
        assert_eq!(resolve_device_node(&root.path().join("ptp")), None);
        assert_eq!(resolve_device_node(&root.path().join("pps")), None);
    }

    #[test]
    fn reads_identity_and_dynamic_sysfs_inventory() {
        let root = TempDir::new().unwrap();
        let pci = root.path().join("0000:03:00.0");
        let card = pci.join("timecard").join("ocp0");
        fs::create_dir_all(card.join("gen1")).unwrap();
        fs::create_dir_all(card.join("freq1")).unwrap();
        fs::write(pci.join("vendor"), "0x1d9b\n").unwrap();
        fs::write(pci.join("device"), "0x0400\n").unwrap();
        fs::write(card.join("serialnum"), "serial-1\n").unwrap();
        fs::write(card.join("clock_source"), "PPS\n").unwrap();
        fs::write(card.join("gnss_sync"), "SYNC\n").unwrap();
        fs::write(card.join("sma1"), "OUT: PHC\n").unwrap();
        fs::write(card.join("gen1/running"), "1\n").unwrap();
        fs::write(card.join("freq1/seconds"), "1\n").unwrap();
        fs::write(card.join("freq1/frequency"), "10000000\n").unwrap();
        fs::write(card.join("ptp"), "/dev/ptp999999\n").unwrap();
        fs::create_dir_all(root.path().join("timecard")).unwrap();
        symlink(&card, root.path().join("timecard/ocp0")).unwrap();

        let snapshot = backend(&root).read_snapshot();
        assert!(snapshot.connected);
        assert_eq!(snapshot.pci_address, "0000:03:00.0");
        assert_eq!(snapshot.serial_number, "serial-1");
        assert_eq!(snapshot.sma_states, ["SMA1 | OUT: PHC"]);
        assert!(snapshot.generator_states[0].contains("running"));
        assert!(snapshot.frequency_counter_states[0].contains("10000000 Hz"));
    }

    #[test]
    fn caches_static_identity_between_samples() {
        let root = TempDir::new().unwrap();
        let card = root.path().join("timecard/ocp5");
        fs::create_dir_all(&card).unwrap();
        fs::write(card.join("serialnum"), "first\n").unwrap();

        let mut backend = backend(&root);
        assert_eq!(backend.read_snapshot().serial_number, "first");
        fs::write(card.join("serialnum"), "second\n").unwrap();
        assert_eq!(backend.read_snapshot().serial_number, "first");
    }

    #[test]
    fn scopes_r4006_standard_subsystems_to_selected_card() {
        let root = TempDir::new().unwrap();
        let selected_pci = root.path().join("devices/0000:03:00.0");
        let card = selected_pci.join("timecard/ocp0");
        let class_timecard = root.path().join("class/timecard");
        let class_hwmon = root.path().join("class/hwmon");
        let class_iio = root.path().join("class/iio");
        let leds = root.path().join("leds");
        for directory in [&card, &class_timecard, &class_hwmon, &class_iio, &leds] {
            fs::create_dir_all(directory).unwrap();
        }
        fs::write(selected_pci.join("vendor"), "0x1d9b\n").unwrap();
        fs::write(selected_pci.join("device"), "0x0400\n").unwrap();

        let adapter = selected_pci.join("i2c-4");
        let mux = adapter.join("4-0070");
        fs::create_dir_all(&mux).unwrap();
        for bus in 5..=7 {
            fs::create_dir_all(adapter.join(format!("i2c-{bus}"))).unwrap();
        }
        symlink(adapter.join("i2c-5"), mux.join("channel-0")).unwrap();
        symlink(adapter.join("i2c-6"), mux.join("channel-1")).unwrap();
        symlink(adapter.join("i2c-7"), mux.join("channel-2")).unwrap();
        symlink(&adapter, card.join("i2c")).unwrap();
        symlink(&card, class_timecard.join("ocp0")).unwrap();

        for (index, bus, address, name, temperature) in [
            (0, 5, "48", "lm75", "41250\n"),
            (1, 5, "49", "lm75", "42000\n"),
            (2, 5, "4a", "lm75", "40875\n"),
            (3, 6, "44", "sht3x", "39750\n"),
        ] {
            let monitor = adapter.join(format!("i2c-{bus}/{bus}-00{address}/hwmon/hwmon{index}"));
            fs::create_dir_all(&monitor).unwrap();
            fs::write(monitor.join("name"), format!("{name}\n")).unwrap();
            fs::write(monitor.join("temp1_input"), temperature).unwrap();
            if name == "sht3x" {
                fs::write(monitor.join("humidity1_input"), "44200\n").unwrap();
            }
            symlink(&monitor, class_hwmon.join(format!("hwmon{index}"))).unwrap();
        }

        let iio = adapter.join("i2c-7/7-0063/iio/iio:device0");
        fs::create_dir_all(&iio).unwrap();
        fs::write(iio.join("name"), "icp10100\n").unwrap();
        fs::write(iio.join("in_pressure_input"), "101.325000\n").unwrap();
        fs::write(iio.join("in_temp_raw"), "32768\n").unwrap();
        symlink(&iio, class_iio.join("iio:device0")).unwrap();

        let led = leds.join("timecard-0000-03-00-0:rgb:indicator-gnss1");
        fs::create_dir_all(&led).unwrap();
        fs::write(led.join("brightness"), "128\n").unwrap();
        fs::write(led.join("max_brightness"), "255\n").unwrap();
        fs::write(led.join("multi_index"), "red green blue\n").unwrap();
        fs::write(led.join("multi_intensity"), "0 255 0\n").unwrap();

        let mut backend = LinuxTimeCardBackend::new(class_timecard, class_hwmon, class_iio, leds);
        let snapshot = backend.read_snapshot();
        assert!(snapshot.r4006_topology_detected);
        assert_eq!(snapshot.sensor_states.len(), 7);
        assert!(snapshot.sensor_states.join("\n").contains("41.25 C"));
        assert!(snapshot.sensor_states.join("\n").contains("44.20%"));
        assert!(snapshot.sensor_states.join("\n").contains("101.325 kPa"));
        assert!(snapshot.sensor_states.join("\n").contains("42.50 C"));
        assert_eq!(snapshot.led_states.len(), 1);
        assert!(snapshot.led_states[0].contains("RGB 0 255 0"));
        assert_eq!(
            snapshot.board_profile,
            "R4006-compatible peripheral profile"
        );
    }

    #[test]
    fn integer_parser_supports_kernel_hex_values() {
        assert_eq!(parse_integer("0x1ad7"), Some(0x1ad7));
        assert_eq!(parse_integer("-12"), Some(-12));
        assert_eq!(parse_integer("garbage"), None);
    }
}
