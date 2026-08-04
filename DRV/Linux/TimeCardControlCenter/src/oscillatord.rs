use std::io::{Read, Write};
use std::net::{IpAddr, SocketAddr, TcpStream, ToSocketAddrs};
use std::sync::OnceLock;
use std::sync::mpsc::{self, RecvTimeoutError, TrySendError};
use std::thread;
use std::time::{Duration, Instant};

use serde_json::{Map, Value};

const MAXIMUM_RESPONSE_BYTES: usize = 1024 * 1024;
const STATUS_REQUEST: &[u8] = br#"{"request":0}"#;

struct ResolveRequest {
    host: String,
    port: u16,
    response: mpsc::SyncSender<std::io::Result<Vec<SocketAddr>>>,
}

static RESOLVER: OnceLock<Option<mpsc::SyncSender<ResolveRequest>>> = OnceLock::new();

#[derive(Clone, Debug, Default, PartialEq)]
pub struct OscillatordTelemetry {
    pub service_version: String,
    pub action_requested: String,
    pub control_enabled: bool,
    pub clock_class: String,
    pub clock_offset_nanoseconds: i64,
    pub discipline: Option<DisciplineTelemetry>,
    pub oscillator_model: String,
    pub fine_control: Option<i64>,
    pub coarse_control: Option<i64>,
    pub oscillator_locked: bool,
    pub oscillator_temperature_celsius: f64,
    pub gnss_fix: Option<i64>,
    pub gnss_fix_ok: bool,
    pub antenna_power: Option<i64>,
    pub antenna_status: Option<i64>,
    pub leap_second_change: Option<i64>,
    pub leap_seconds: Option<i64>,
    pub satellites: i64,
    pub survey_position_error_meters: Option<f64>,
    pub time_accuracy_nanoseconds: Option<i64>,
    pub service_error: String,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct DisciplineTelemetry {
    pub status: String,
    pub current_convergence_count: Option<i64>,
    pub convergence_threshold: Option<i64>,
    pub convergence_progress: f64,
    pub ready_for_holdover: bool,
}

impl OscillatordTelemetry {
    /// Performs one bounded protocol v1 status request.
    ///
    /// # Errors
    ///
    /// Returns a connection, framing, JSON, or protocol-validation error.
    pub fn poll(host: &str, port: u16, timeout: Duration) -> Result<Self, OscillatordError> {
        let deadline = Instant::now()
            .checked_add(timeout)
            .ok_or(OscillatordError::Timeout)?;
        let addresses = resolve_endpoint(host, port, deadline)?;
        let mut last_error = None;
        let mut stream = None;
        for address in addresses {
            match TcpStream::connect_timeout(&address, remaining(deadline)?) {
                Ok(connection) => {
                    stream = Some(connection);
                    break;
                }
                Err(error) => last_error = Some(error),
            }
        }
        let mut stream = stream.ok_or_else(|| {
            OscillatordError::Connect(last_error.unwrap_or_else(|| {
                std::io::Error::new(
                    std::io::ErrorKind::AddrNotAvailable,
                    "host resolved to no addresses",
                )
            }))
        })?;
        stream.set_write_timeout(Some(remaining(deadline)?))?;
        stream.write_all(STATUS_REQUEST).map_err(transport_error)?;
        stream.flush().map_err(transport_error)?;

        let mut response = Vec::with_capacity(4096);
        let mut buffer = [0_u8; 4096];
        loop {
            stream.set_read_timeout(Some(remaining(deadline)?))?;
            let count = stream.read(&mut buffer).map_err(transport_error)?;
            if count == 0 {
                break;
            }
            if response.len() + count > MAXIMUM_RESPONSE_BYTES {
                return Err(OscillatordError::OversizedResponse);
            }
            response.extend_from_slice(&buffer[..count]);
            match serde_json::from_slice::<Value>(&response) {
                Ok(value) => return Self::from_value(&value),
                Err(error) if error.is_eof() => {}
                Err(error) => return Err(OscillatordError::Json(error)),
            }
        }
        let value = serde_json::from_slice(&response)?;
        Self::from_value(&value)
    }

    /// Parses and validates a complete oscillatord protocol v1 response.
    ///
    /// # Errors
    ///
    /// Returns a protocol error when metadata or required telemetry is absent or mistyped.
    pub fn from_slice(response: &[u8]) -> Result<Self, OscillatordError> {
        if response.len() > MAXIMUM_RESPONSE_BYTES {
            return Err(OscillatordError::OversizedResponse);
        }
        let value = serde_json::from_slice(response)?;
        Self::from_value(&value)
    }

    fn from_value(root: &Value) -> Result<Self, OscillatordError> {
        let root = object(root, "root response")?;
        if string(root, "service")? != "oscillatord" {
            return Err(protocol("unexpected monitoring service response"));
        }
        let service_version = string(root, "version")?.to_owned();
        if integer(root, "protocol_version")? != 1 {
            return Err(protocol("unsupported oscillatord monitoring protocol"));
        }
        let clock = object_field(root, "clock")?;
        let oscillator = object_field(root, "oscillator")?;
        let gnss = object_field(root, "gnss")?;

        let discipline = match root.get("disciplining") {
            None | Some(Value::Null) => None,
            Some(value) => {
                let value = object(value, "disciplining")?;
                Some(DisciplineTelemetry {
                    status: string(value, "status")?.to_owned(),
                    current_convergence_count: optional_integer(
                        value,
                        "current_phase_convergence_count",
                    )?
                    .filter(|count| *count >= 0),
                    convergence_threshold: optional_integer(
                        value,
                        "valid_phase_convergence_threshold",
                    )?
                    .filter(|threshold| *threshold > 0),
                    convergence_progress: number(value, "convergence_progress")?.clamp(0.0, 100.0),
                    ready_for_holdover: boolean(value, "ready_for_holdover")?,
                })
            }
        };

        Ok(Self {
            service_version,
            action_requested: optional_string(root, "Action requested")
                .unwrap_or_default()
                .to_owned(),
            control_enabled: optional_boolean(root, "control_enabled")?.unwrap_or(false),
            clock_class: string(clock, "class")?.to_owned(),
            clock_offset_nanoseconds: integer(clock, "offset")?,
            discipline,
            oscillator_model: string(oscillator, "model")?.to_owned(),
            fine_control: valid_control(optional_integer(oscillator, "fine_ctrl")?),
            coarse_control: valid_control(optional_integer(oscillator, "coarse_ctrl")?),
            oscillator_locked: boolean(oscillator, "lock")?,
            oscillator_temperature_celsius: number(oscillator, "temperature")?,
            gnss_fix: optional_integer(gnss, "fix")?.filter(|fix| *fix >= 0),
            gnss_fix_ok: boolean(gnss, "fixOk")?,
            antenna_power: optional_integer(gnss, "antenna_power")?.filter(|value| *value >= 0),
            antenna_status: optional_integer(gnss, "antenna_status")?.filter(|value| *value >= 0),
            leap_second_change: optional_integer(gnss, "lsChange")?.filter(|value| *value != -10),
            leap_seconds: optional_integer(gnss, "leap_seconds")?.filter(|value| *value >= 0),
            satellites: integer(gnss, "satellites_count")?,
            survey_position_error_meters: optional_number(gnss, "survey_in_position_error")?
                .filter(|value| *value >= 0.0),
            time_accuracy_nanoseconds: optional_integer(gnss, "time_accuracy")?
                .filter(|value| *value >= 0),
            service_error: optional_string(root, "error")
                .unwrap_or_default()
                .to_owned(),
        })
    }
}

#[derive(Debug, thiserror::Error)]
pub enum OscillatordError {
    #[error("could not resolve the oscillatord endpoint: {0}")]
    Resolve(std::io::Error),
    #[error("could not connect to oscillatord: {0}")]
    Connect(std::io::Error),
    #[error("oscillatord status request timed out")]
    Timeout,
    #[error("oscillatord transport failed: {0}")]
    Io(#[from] std::io::Error),
    #[error("oscillatord returned invalid JSON: {0}")]
    Json(#[from] serde_json::Error),
    #[error("oscillatord returned an oversized response")]
    OversizedResponse,
    #[error("oscillatord protocol error: {0}")]
    Protocol(String),
}

#[must_use]
pub fn display_endpoint(host: &str, port: u16) -> String {
    if host.contains(':') && !host.starts_with('[') {
        format!("[{host}]:{port}")
    } else {
        format!("{host}:{port}")
    }
}

fn protocol(message: &str) -> OscillatordError {
    OscillatordError::Protocol(message.to_owned())
}

fn valid_control(value: Option<i64>) -> Option<i64> {
    value.filter(|value| *value >= 0 && *value != i64::from(u32::MAX))
}

fn resolve_endpoint(
    host: &str,
    port: u16,
    deadline: Instant,
) -> Result<Vec<SocketAddr>, OscillatordError> {
    if let Ok(address) = host.parse::<IpAddr>() {
        return Ok(vec![SocketAddr::new(address, port)]);
    }

    let (response, receiver) = mpsc::sync_channel(1);
    let request = ResolveRequest {
        host: host.to_owned(),
        port,
        response,
    };
    let resolver = resolver().ok_or_else(|| {
        OscillatordError::Resolve(std::io::Error::other(
            "the endpoint resolver could not start",
        ))
    })?;
    match resolver.try_send(request) {
        Ok(()) => {}
        Err(TrySendError::Full(_)) => return Err(OscillatordError::Timeout),
        Err(TrySendError::Disconnected(_)) => {
            return Err(OscillatordError::Resolve(std::io::Error::other(
                "the endpoint resolver is unavailable",
            )));
        }
    }

    match receiver.recv_timeout(remaining(deadline)?) {
        Ok(Ok(addresses)) => Ok(addresses),
        Ok(Err(error)) => Err(OscillatordError::Resolve(error)),
        Err(RecvTimeoutError::Timeout) => Err(OscillatordError::Timeout),
        Err(RecvTimeoutError::Disconnected) => Err(OscillatordError::Resolve(
            std::io::Error::other("the endpoint resolver stopped unexpectedly"),
        )),
    }
}

fn resolver() -> Option<&'static mpsc::SyncSender<ResolveRequest>> {
    RESOLVER
        .get_or_init(|| {
            let (sender, receiver) = mpsc::sync_channel::<ResolveRequest>(1);
            let worker = thread::Builder::new()
                .name("oscillatord-resolver".to_owned())
                .spawn(move || {
                    while let Ok(request) = receiver.recv() {
                        let result = (request.host.as_str(), request.port)
                            .to_socket_addrs()
                            .map(Iterator::collect::<Vec<_>>);
                        let _ = request.response.send(result);
                    }
                });
            worker.ok().map(|_| sender)
        })
        .as_ref()
}

fn remaining(deadline: Instant) -> Result<Duration, OscillatordError> {
    let remaining = deadline.saturating_duration_since(Instant::now());
    if remaining.is_zero() {
        Err(OscillatordError::Timeout)
    } else {
        Ok(remaining)
    }
}

fn transport_error(error: std::io::Error) -> OscillatordError {
    if matches!(
        error.kind(),
        std::io::ErrorKind::TimedOut | std::io::ErrorKind::WouldBlock
    ) {
        OscillatordError::Timeout
    } else {
        OscillatordError::Io(error)
    }
}

fn object<'a>(value: &'a Value, name: &str) -> Result<&'a Map<String, Value>, OscillatordError> {
    value
        .as_object()
        .ok_or_else(|| protocol(&format!("{name} must be an object")))
}

fn object_field<'a>(
    object: &'a Map<String, Value>,
    name: &str,
) -> Result<&'a Map<String, Value>, OscillatordError> {
    object
        .get(name)
        .ok_or_else(|| protocol(&format!("missing {name}")))
        .and_then(|value| self::object(value, name))
}

fn string<'a>(object: &'a Map<String, Value>, name: &str) -> Result<&'a str, OscillatordError> {
    object
        .get(name)
        .and_then(Value::as_str)
        .ok_or_else(|| protocol(&format!("{name} must be a string")))
}

fn optional_string<'a>(object: &'a Map<String, Value>, name: &str) -> Option<&'a str> {
    object.get(name).and_then(Value::as_str)
}

fn integer(object: &Map<String, Value>, name: &str) -> Result<i64, OscillatordError> {
    object
        .get(name)
        .and_then(Value::as_i64)
        .ok_or_else(|| protocol(&format!("{name} must be an integer")))
}

fn optional_integer(
    object: &Map<String, Value>,
    name: &str,
) -> Result<Option<i64>, OscillatordError> {
    match object.get(name) {
        None | Some(Value::Null) => Ok(None),
        Some(value) => value
            .as_i64()
            .map(Some)
            .ok_or_else(|| protocol(&format!("{name} must be an integer"))),
    }
}

fn number(object: &Map<String, Value>, name: &str) -> Result<f64, OscillatordError> {
    object
        .get(name)
        .and_then(Value::as_f64)
        .filter(|value| value.is_finite())
        .ok_or_else(|| protocol(&format!("{name} must be a finite number")))
}

fn optional_number(
    object: &Map<String, Value>,
    name: &str,
) -> Result<Option<f64>, OscillatordError> {
    match object.get(name) {
        None | Some(Value::Null) => Ok(None),
        Some(value) => value
            .as_f64()
            .filter(|value| value.is_finite())
            .map(Some)
            .ok_or_else(|| protocol(&format!("{name} must be a finite number"))),
    }
}

fn boolean(object: &Map<String, Value>, name: &str) -> Result<bool, OscillatordError> {
    object
        .get(name)
        .and_then(Value::as_bool)
        .ok_or_else(|| protocol(&format!("{name} must be a boolean")))
}

fn optional_boolean(
    object: &Map<String, Value>,
    name: &str,
) -> Result<Option<bool>, OscillatordError> {
    match object.get(name) {
        None | Some(Value::Null) => Ok(None),
        Some(value) => value
            .as_bool()
            .map(Some)
            .ok_or_else(|| protocol(&format!("{name} must be a boolean"))),
    }
}

#[cfg(test)]
mod tests {
    use std::net::TcpListener;
    use std::thread;

    use super::*;

    const RESPONSE: &[u8] = br#"{
        "service":"oscillatord",
        "version":"9.9.0",
        "protocol_version":1,
        "Action requested":"None",
        "control_enabled":true,
        "clock":{"class":"locked","offset":-12},
        "oscillator":{"model":"mRO-50","lock":true,"temperature":42.5,"fine_ctrl":10},
        "gnss":{"fix":3,"fixOk":true,"satellites_count":8,"time_accuracy":25},
        "disciplining":{
            "status":"tracking",
            "convergence_progress":67.5,
            "ready_for_holdover":false,
            "current_phase_convergence_count":27,
            "valid_phase_convergence_threshold":40
        }
    }"#;

    #[test]
    fn parses_protocol_v1_telemetry() {
        let telemetry = OscillatordTelemetry::from_slice(RESPONSE).unwrap();
        assert_eq!(telemetry.service_version, "9.9.0");
        assert_eq!(telemetry.clock_offset_nanoseconds, -12);
        assert_eq!(telemetry.satellites, 8);
        assert_eq!(telemetry.discipline.unwrap().convergence_progress, 67.5);
    }

    #[test]
    fn normalizes_protocol_sentinels_to_unavailable_values() {
        let response = br#"{
            "service":"oscillatord",
            "version":"9.9.0",
            "protocol_version":1,
            "clock":{"class":"unlocked","offset":0},
            "oscillator":{
                "model":"unknown",
                "lock":false,
                "temperature":-400,
                "fine_ctrl":4294967295,
                "coarse_ctrl":-1
            },
            "gnss":{
                "fix":-1,
                "fixOk":false,
                "antenna_power":-1,
                "antenna_status":-1,
                "lsChange":-10,
                "leap_seconds":-1,
                "satellites_count":-1,
                "survey_in_position_error":-1,
                "time_accuracy":-1
            }
        }"#;

        let telemetry = OscillatordTelemetry::from_slice(response).unwrap();
        assert_eq!(telemetry.fine_control, None);
        assert_eq!(telemetry.coarse_control, None);
        assert_eq!(telemetry.gnss_fix, None);
        assert_eq!(telemetry.antenna_power, None);
        assert_eq!(telemetry.antenna_status, None);
        assert_eq!(telemetry.leap_second_change, None);
        assert_eq!(telemetry.leap_seconds, None);
        assert_eq!(telemetry.survey_position_error_meters, None);
        assert_eq!(telemetry.time_accuracy_nanoseconds, None);
    }

    #[test]
    fn rejects_wrong_service() {
        let response = RESPONSE
            .windows(b"oscillatord".len())
            .position(|window| window == b"oscillatord")
            .map(|index| {
                let mut copy = RESPONSE.to_vec();
                copy[index..index + b"oscillatord".len()].copy_from_slice(b"unexpected!");
                copy
            })
            .unwrap();
        assert!(matches!(
            OscillatordTelemetry::from_slice(&response),
            Err(OscillatordError::Protocol(_))
        ));
    }

    #[test]
    fn formats_ipv6_endpoint() {
        assert_eq!(display_endpoint("::1", 2958), "[::1]:2958");
    }

    #[test]
    fn reads_a_chunked_status_response() {
        let listener = TcpListener::bind(("127.0.0.1", 0)).unwrap();
        let port = listener.local_addr().unwrap().port();
        let server = thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            let mut request = [0_u8; STATUS_REQUEST.len()];
            stream.read_exact(&mut request).unwrap();
            assert_eq!(request, STATUS_REQUEST);
            for chunk in RESPONSE.chunks(23) {
                stream.write_all(chunk).unwrap();
            }
        });

        let telemetry =
            OscillatordTelemetry::poll("127.0.0.1", port, Duration::from_secs(1)).unwrap();
        assert_eq!(telemetry.oscillator_model, "mRO-50");
        server.join().unwrap();
    }

    #[test]
    fn enforces_an_absolute_response_deadline() {
        let listener = TcpListener::bind(("127.0.0.1", 0)).unwrap();
        let port = listener.local_addr().unwrap().port();
        let server = thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            let mut request = [0_u8; STATUS_REQUEST.len()];
            stream.read_exact(&mut request).unwrap();
            assert_eq!(request, STATUS_REQUEST);
            for chunk in [b'{'].into_iter().chain(std::iter::repeat_n(b' ', 10)) {
                if stream.write_all(&[chunk]).is_err() {
                    break;
                }
                thread::sleep(Duration::from_millis(30));
            }
        });

        let started = Instant::now();
        let result = OscillatordTelemetry::poll("127.0.0.1", port, Duration::from_millis(120));
        assert!(matches!(result, Err(OscillatordError::Timeout)));
        assert!(started.elapsed() < Duration::from_secs(1));
        server.join().unwrap();
    }

    #[test]
    fn rejects_an_unsupported_protocol_version() {
        let response = String::from_utf8(RESPONSE.to_vec())
            .unwrap()
            .replace("\"protocol_version\":1", "\"protocol_version\":2");
        assert!(matches!(
            OscillatordTelemetry::from_slice(response.as_bytes()),
            Err(OscillatordError::Protocol(_))
        ));
    }
}
