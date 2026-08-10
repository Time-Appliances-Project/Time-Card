#![cfg(all(feature = "tui", unix))]

use std::fs::File;
use std::io::Read;
use std::os::fd::{AsRawFd, FromRawFd};
use std::process::{Child, Command, ExitStatus, Stdio};
use std::thread;
use std::time::{Duration, Instant};

const BINARY: &str = env!("CARGO_BIN_EXE_timecard-control-center-tui");

#[test]
fn signal_exit_restores_terminal_mode_and_screen() {
    let (master, slave) = open_pty(100, 30);
    let original_mode = terminal_mode(slave.as_raw_fd());
    let reader = thread::spawn(move || {
        let mut master = master;
        let mut output = Vec::new();
        let mut buffer = [0_u8; 4096];
        loop {
            match master.read(&mut buffer) {
                Ok(0) => break,
                Ok(count) => output.extend_from_slice(&buffer[..count]),
                Err(error) if error.raw_os_error() == Some(libc::EIO) => break,
                Err(error) => panic!("PTY read failed: {error}"),
            }
        }
        output
    });

    let mut child = Command::new(BINARY)
        .args(["--mock", "--quit-after", "5000"])
        .stdin(Stdio::from(slave.try_clone().unwrap()))
        .stdout(Stdio::from(slave.try_clone().unwrap()))
        .stderr(Stdio::from(slave.try_clone().unwrap()))
        .spawn()
        .unwrap();

    if !wait_for_raw_mode(slave.as_raw_fd(), Duration::from_secs(3)) {
        let _ = child.kill();
        let _ = child.wait();
        panic!("TUI did not enter raw mode");
    }

    send_signal(&child, libc::SIGWINCH);
    thread::sleep(Duration::from_millis(50));
    send_signal(&child, libc::SIGINT);
    let status = wait_for_exit(&mut child, Duration::from_secs(3));
    let restored_mode = terminal_mode(slave.as_raw_fd());
    drop(slave);
    let output = reader.join().unwrap();

    assert!(status.success(), "TUI exited with {status}");
    assert!(same_terminal_mode(&original_mode, &restored_mode));
    assert!(contains_bytes(&output, b"\x1b[?1049h"));
    assert!(contains_bytes(&output, b"\x1b[?1049l"));
}

fn open_pty(columns: u16, rows: u16) -> (File, File) {
    let mut master = -1;
    let mut slave = -1;
    let mut size = libc::winsize {
        ws_row: rows,
        ws_col: columns,
        ws_xpixel: 0,
        ws_ypixel: 0,
    };
    // SAFETY: all pointers refer to initialized writable storage, and successful descriptors are
    // immediately transferred into File for ownership.
    let result = unsafe {
        libc::openpty(
            &raw mut master,
            &raw mut slave,
            std::ptr::null_mut(),
            std::ptr::null_mut(),
            &raw mut size,
        )
    };
    assert_eq!(
        result,
        0,
        "openpty failed: {}",
        std::io::Error::last_os_error()
    );
    // SAFETY: openpty returned two new, owned file descriptors.
    unsafe { (File::from_raw_fd(master), File::from_raw_fd(slave)) }
}

fn terminal_mode(file: libc::c_int) -> libc::termios {
    // SAFETY: the zeroed value is immediately initialized by tcgetattr before it is returned.
    unsafe {
        let mut mode = std::mem::zeroed();
        assert_eq!(
            libc::tcgetattr(file, &raw mut mode),
            0,
            "tcgetattr failed: {}",
            std::io::Error::last_os_error()
        );
        mode
    }
}

fn same_terminal_mode(left: &libc::termios, right: &libc::termios) -> bool {
    left.c_iflag == right.c_iflag
        && left.c_oflag == right.c_oflag
        && left.c_cflag == right.c_cflag
        && left.c_lflag == right.c_lflag
        && left.c_cc == right.c_cc
}

fn wait_for_raw_mode(file: libc::c_int, timeout: Duration) -> bool {
    let deadline = Instant::now() + timeout;
    while Instant::now() < deadline {
        let mode = terminal_mode(file);
        if mode.c_lflag & (libc::ICANON | libc::ECHO) == 0 {
            return true;
        }
        thread::sleep(Duration::from_millis(20));
    }
    false
}

fn send_signal(child: &Child, signal: libc::c_int) {
    // SAFETY: kill is called with the live child process ID and a valid POSIX signal.
    let result = unsafe { libc::kill(child.id() as libc::pid_t, signal) };
    assert_eq!(
        result,
        0,
        "kill failed: {}",
        std::io::Error::last_os_error()
    );
}

fn wait_for_exit(child: &mut Child, timeout: Duration) -> ExitStatus {
    let deadline = Instant::now() + timeout;
    while Instant::now() < deadline {
        if let Some(status) = child.try_wait().unwrap() {
            return status;
        }
        thread::sleep(Duration::from_millis(20));
    }
    let _ = child.kill();
    let status = child.wait().unwrap();
    panic!("TUI did not exit after signal, final status {status}");
}

fn contains_bytes(haystack: &[u8], needle: &[u8]) -> bool {
    haystack
        .windows(needle.len())
        .any(|window| window == needle)
}
