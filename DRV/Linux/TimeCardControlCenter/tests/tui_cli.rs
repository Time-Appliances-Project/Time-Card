#![cfg(feature = "tui")]

use std::io::{Read, Write};
use std::net::TcpListener;
use std::process::{Command, Output};
use std::thread;
use std::time::Duration;

const BINARY: &str = env!("CARGO_BIN_EXE_timecard-control-center-tui");

const OSCILLATORD_RESPONSE: &[u8] = br#"{
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
fn plain_mock_output_is_complete_and_ansi_free() {
    let output = plain_output("timing-io");
    assert!(output.status.success(), "{}", stderr(&output));
    assert!(output.stderr.is_empty(), "{}", stderr(&output));
    assert!(output.stdout.ends_with(b"\n"));
    assert!(!output.stdout.contains(&0x1b));
    let text = String::from_utf8(output.stdout).unwrap();
    assert!(text.contains("Timing I/O"));
    assert!(text.contains("mock0"));
    assert!(text.contains("IRIG/DCF"));
}

#[test]
fn help_is_available_in_plain_mode() {
    let output = plain_output("help");
    assert!(output.status.success(), "{}", stderr(&output));
    let text = String::from_utf8(output.stdout).unwrap();
    assert!(text.contains("HELP"));
    assert!(text.contains("Quit and restore the terminal"));
}

#[test]
fn invalid_port_and_non_terminal_interactive_mode_fail_fast() {
    let invalid_port = Command::new(BINARY)
        .args(["--mock", "--plain", "--oscillatord-port", "0"])
        .output()
        .unwrap();
    assert_eq!(invalid_port.status.code(), Some(2));

    let interactive = Command::new(BINARY).arg("--mock").output().unwrap();
    assert_eq!(interactive.status.code(), Some(2));
    assert!(stderr(&interactive).contains("use --plain"));
}

#[test]
fn plain_oscillatord_waits_for_a_bounded_service_response() {
    let listener = TcpListener::bind(("127.0.0.1", 0)).unwrap();
    let port = listener.local_addr().unwrap().port();
    let server = thread::spawn(move || {
        let (mut stream, _) = listener.accept().unwrap();
        let mut request = [0_u8; 13];
        stream.read_exact(&mut request).unwrap();
        assert_eq!(&request, br#"{"request":0}"#);
        thread::sleep(Duration::from_millis(100));
        stream.write_all(OSCILLATORD_RESPONSE).unwrap();
    });

    let output = Command::new(BINARY)
        .args([
            "--mock",
            "--plain",
            "--page",
            "oscillatord",
            "--oscillatord-port",
            &port.to_string(),
        ])
        .output()
        .unwrap();
    server.join().unwrap();

    assert!(output.status.success(), "{}", stderr(&output));
    let text = String::from_utf8(output.stdout).unwrap();
    assert!(text.contains("oscillatord 9.9.0"));
    assert!(text.contains("Status action: None"));
}

fn plain_output(page: &str) -> Output {
    Command::new(BINARY)
        .args(["--mock", "--plain", "--page", page])
        .output()
        .unwrap()
}

fn stderr(output: &Output) -> String {
    String::from_utf8_lossy(&output.stderr).into_owned()
}
