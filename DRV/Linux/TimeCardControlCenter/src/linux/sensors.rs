use std::fs;
use std::path::{Path, PathBuf};

use crate::snapshot::TimeCardSnapshot;

use super::{add_unique, labeled, read_optional_text, read_text};

#[derive(Clone, Debug)]
struct HwmonExpectation {
    client_path: PathBuf,
    driver_name: &'static str,
    label: &'static str,
    humidity: bool,
}

pub(super) fn read_standard_sensors(
    hwmon_root: &Path,
    iio_root: &Path,
    snapshot: &mut TimeCardSnapshot,
) {
    let Some(adapter_path) = canonical(&Path::new(&snapshot.sysfs_path).join("i2c")) else {
        return;
    };
    let Some(adapter_number) = adapter_path
        .file_name()
        .and_then(|name| name.to_str())
        .and_then(|name| name.strip_prefix("i2c-"))
        .filter(|number| number.chars().all(|character| character.is_ascii_digit()))
    else {
        return;
    };
    let mux_path = adapter_path.join(format!("{adapter_number}-0070"));
    let client_path = |channel: u8, address: &str| -> PathBuf {
        canonical(&mux_path.join(format!("channel-{channel}")))
            .and_then(|channel_path| {
                channel_path
                    .file_name()
                    .and_then(|name| name.to_str())
                    .and_then(|name| name.strip_prefix("i2c-"))
                    .filter(|number| number.chars().all(|character| character.is_ascii_digit()))
                    .map(|number| channel_path.join(format!("{number}-00{address}")))
            })
            .and_then(|path| canonical(&path))
            .unwrap_or_default()
    };

    let expected_hwmon = [
        HwmonExpectation {
            client_path: client_path(0, "48"),
            driver_name: "lm75",
            label: "LM75B 0x48",
            humidity: false,
        },
        HwmonExpectation {
            client_path: client_path(0, "49"),
            driver_name: "lm75",
            label: "LM75B 0x49",
            humidity: false,
        },
        HwmonExpectation {
            client_path: client_path(0, "4a"),
            driver_name: "lm75",
            label: "LM75B 0x4a",
            humidity: false,
        },
        HwmonExpectation {
            client_path: client_path(1, "44"),
            driver_name: "sht3x",
            label: "SHT3x 0x44",
            humidity: true,
        },
    ];
    let expected_icp = client_path(2, "63");
    snapshot.r4006_topology_detected = mux_path.exists()
        && expected_hwmon.iter().all(|expected| {
            !expected.client_path.as_os_str().is_empty() && expected.client_path.exists()
        })
        && !expected_icp.as_os_str().is_empty()
        && expected_icp.exists();

    for monitor in matching_directories(hwmon_root, "hwmon") {
        let Some(expected) = expected_hwmon
            .iter()
            .find(|expected| belongs_to_client(&monitor, &expected.client_path))
        else {
            continue;
        };
        if !read_text(&monitor.join("name"))
            .to_ascii_lowercase()
            .starts_with(expected.driver_name)
        {
            continue;
        }

        let temperature_inputs = matching_files(&monitor, "temp", "_input");
        for input in &temperature_inputs {
            let stem = input
                .file_name()
                .and_then(|name| name.to_str())
                .and_then(|name| name.strip_suffix("_input"))
                .unwrap_or("temperature");
            let label = if temperature_inputs.len() == 1 {
                expected.label.to_owned()
            } else {
                format!("{} {}", expected.label, stem.trim_start_matches("temp"))
            };
            if let Some(raw) = read_measurement(input, &label, snapshot) {
                snapshot
                    .sensor_states
                    .push(labeled(&label, &format!("{:.2} C", raw / 1000.0)));
            }
        }

        if expected.humidity {
            for input in matching_files(&monitor, "humidity", "_input") {
                let label = "SHT3x 0x44 humidity";
                if let Some(raw) = read_measurement(&input, label, snapshot) {
                    snapshot
                        .sensor_states
                        .push(labeled(label, &format!("{:.2}%", raw / 1000.0)));
                }
            }
            let temperature_alarm = read_optional_text(&monitor.join("temp1_alarm"));
            let humidity_alarm = read_optional_text(&monitor.join("humidity1_alarm"));
            if temperature_alarm.is_some() || humidity_alarm.is_some() {
                let mut alarms = Vec::new();
                if let Some(value) = temperature_alarm {
                    alarms.push(if value == "0" {
                        "temperature clear"
                    } else {
                        "temperature alarm"
                    });
                }
                if let Some(value) = humidity_alarm {
                    alarms.push(if value == "0" {
                        "humidity clear"
                    } else {
                        "humidity alarm"
                    });
                }
                snapshot
                    .sensor_states
                    .push(labeled("SHT3x alarms", &alarms.join(" | ")));
            }
        }
    }

    for device in matching_directories(iio_root, "iio:device") {
        if !belongs_to_client(&device, &expected_icp)
            || !read_text(&device.join("name")).eq_ignore_ascii_case("icp10100")
        {
            continue;
        }
        if let Some(pressure) = read_measurement(
            &device.join("in_pressure_input"),
            "ICP-10100 pressure",
            snapshot,
        ) {
            snapshot
                .sensor_states
                .push(labeled("ICP-10100 pressure", &format!("{pressure:.3} kPa")));
        }
        if let Some(raw) = read_measurement(
            &device.join("in_temp_raw"),
            "ICP-10100 temperature",
            snapshot,
        ) {
            let celsius = -45.0 + 175.0 * raw / 65_536.0;
            snapshot
                .sensor_states
                .push(labeled("ICP-10100 temperature", &format!("{celsius:.2} C")));
        }
    }

    if !snapshot.sensor_states.is_empty() {
        add_unique(&mut snapshot.capabilities, "Sensors");
    }
}

pub(super) fn read_standard_leds(
    leds_root: &Path,
    sysfs_root: &Path,
    snapshot: &mut TimeCardSnapshot,
) {
    let prefix = led_prefix(&snapshot.pci_address);
    for led in matching_directories(leds_root, "timecard-") {
        let Some(led_name) = led.file_name().and_then(|name| name.to_str()) else {
            continue;
        };
        if let Some(prefix) = &prefix {
            if !led_name.starts_with(prefix) {
                continue;
            }
        } else if !led_name.contains(":rgb:indicator-")
            || !subsystem_entry_belongs_to_card(&led, &snapshot.pci_address, sysfs_root)
        {
            continue;
        }

        let Some(brightness) = read_optional_text(&led.join("brightness")) else {
            continue;
        };
        let Some(maximum) = read_optional_text(&led.join("max_brightness")) else {
            continue;
        };
        let Some(components) = read_optional_text(&led.join("multi_index")) else {
            continue;
        };
        let Some(intensity) = read_optional_text(&led.join("multi_intensity")) else {
            continue;
        };
        let connector = led_name
            .rsplit_once("indicator-")
            .map_or(led_name, |(_, connector)| connector)
            .to_ascii_uppercase();
        let rgb = if components.split_whitespace().collect::<Vec<_>>() == ["red", "green", "blue"] {
            format!(
                "RGB {}",
                intensity.split_whitespace().collect::<Vec<_>>().join(" ")
            )
        } else {
            format!(
                "{} {}",
                components.split_whitespace().collect::<Vec<_>>().join(" "),
                intensity.split_whitespace().collect::<Vec<_>>().join(" ")
            )
        };
        snapshot.led_states.push(labeled(
            &connector,
            &format!("brightness {brightness}/{maximum} | {rgb}"),
        ));
    }
    if !snapshot.led_states.is_empty() {
        add_unique(&mut snapshot.capabilities, "Status LEDs");
    }
}

fn matching_directories(root: &Path, prefix: &str) -> Vec<PathBuf> {
    let mut entries: Vec<PathBuf> = fs::read_dir(root)
        .into_iter()
        .flatten()
        .flatten()
        .filter(|entry| {
            entry
                .file_type()
                .is_ok_and(|kind| kind.is_dir() || kind.is_symlink())
        })
        .filter(|entry| entry.file_name().to_string_lossy().starts_with(prefix))
        .map(|entry| entry.path())
        .collect();
    entries.sort();
    entries
}

fn matching_files(root: &Path, prefix: &str, suffix: &str) -> Vec<PathBuf> {
    let mut entries: Vec<PathBuf> = fs::read_dir(root)
        .into_iter()
        .flatten()
        .flatten()
        .filter(|entry| {
            entry
                .file_type()
                .is_ok_and(|kind| kind.is_file() || kind.is_symlink())
        })
        .filter(|entry| {
            let name = entry.file_name();
            let name = name.to_string_lossy();
            name.starts_with(prefix) && name.ends_with(suffix)
        })
        .map(|entry| entry.path())
        .collect();
    entries.sort();
    entries
}

fn canonical(path: &Path) -> Option<PathBuf> {
    fs::canonicalize(path).ok()
}

fn subsystem_device_path(entry: &Path) -> PathBuf {
    canonical(&entry.join("device"))
        .or_else(|| canonical(entry))
        .unwrap_or_default()
}

fn belongs_to_client(entry: &Path, expected_client: &Path) -> bool {
    if expected_client.as_os_str().is_empty() {
        return false;
    }
    subsystem_device_path(entry).starts_with(expected_client)
}

fn read_measurement(path: &Path, label: &str, snapshot: &mut TimeCardSnapshot) -> Option<f64> {
    let Some(text) = read_optional_text(path) else {
        snapshot.push_error(format!("Sensor read failed for {label}"));
        return None;
    };
    match text.parse::<f64>() {
        Ok(value) if value.is_finite() => Some(value),
        _ => {
            snapshot.push_error(format!("Sensor returned an invalid value for {label}"));
            None
        }
    }
}

fn led_prefix(pci_address: &str) -> Option<String> {
    let (domain, remainder) = pci_address.split_once(':')?;
    let (bus, remainder) = remainder.split_once(':')?;
    let (device, function) = remainder.split_once('.')?;
    let valid_hex = |value: &str, length: usize| {
        value.len() == length && value.chars().all(|character| character.is_ascii_hexdigit())
    };
    if !valid_hex(domain, 4)
        || !valid_hex(bus, 2)
        || !valid_hex(device, 2)
        || function.len() != 1
        || !function
            .chars()
            .all(|character| ('0'..='7').contains(&character))
    {
        return None;
    }
    Some(format!(
        "timecard-{}-{}-{}-{}:rgb:indicator-",
        domain.to_ascii_lowercase(),
        bus.to_ascii_lowercase(),
        device.to_ascii_lowercase(),
        function
    ))
}

fn subsystem_entry_belongs_to_card(entry: &Path, pci_address: &str, sysfs_root: &Path) -> bool {
    if pci_address.is_empty() {
        return sysfs_root != Path::new("/sys/class/timecard");
    }
    let canonical = subsystem_device_path(entry);
    canonical
        .components()
        .any(|component| component.as_os_str() == pci_address)
        || entry.to_string_lossy().contains(pci_address)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builds_bdf_scoped_led_prefix() {
        assert_eq!(
            led_prefix("0000:03:00.0").as_deref(),
            Some("timecard-0000-03-00-0:rgb:indicator-")
        );
        assert_eq!(led_prefix("unsafe"), None);
    }
}
