use std::io;
use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};

#[cfg(target_os = "linux")]
use std::fs::File;

#[cfg(target_os = "linux")]
use crate::timing::NANOSECONDS_PER_SECOND;

#[cfg(target_os = "linux")]
const MAXIMUM_SAMPLING_WINDOW_NANOSECONDS: i64 = NANOSECONDS_PER_SECOND;

#[derive(Clone, Debug, Eq, PartialEq)]
pub(super) struct PhcSample {
    pub phc_tai_nanoseconds: i64,
    pub system_utc_nanoseconds: i64,
    pub sample_window_nanoseconds: Option<i64>,
    pub method: &'static str,
}

#[must_use]
pub(super) fn system_utc_nanoseconds() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0, |duration| {
            duration.as_nanos().min(i64::MAX as u128) as i64
        })
}

#[cfg(target_os = "linux")]
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
struct PtpClockTime {
    sec: i64,
    nsec: u32,
    reserved: u32,
}

#[cfg(target_os = "linux")]
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
struct PtpSysOffsetPrecise {
    device: PtpClockTime,
    sys_realtime: PtpClockTime,
    sys_monoraw: PtpClockTime,
    reserved: [u32; 4],
}

#[cfg(target_os = "linux")]
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
struct PtpSysOffsetExtended {
    n_samples: u32,
    reserved: [u32; 3],
    ts: [[PtpClockTime; 3]; 25],
}

#[cfg(target_os = "linux")]
nix::ioctl_readwrite!(ptp_sys_offset_precise, b'=', 8, PtpSysOffsetPrecise);
#[cfg(target_os = "linux")]
nix::ioctl_readwrite!(ptp_sys_offset_extended, b'=', 9, PtpSysOffsetExtended);

#[cfg(target_os = "linux")]
pub(super) fn sample(path: &Path) -> io::Result<PhcSample> {
    use std::os::fd::AsRawFd;

    let file = File::open(path)?;
    let file_descriptor = file.as_raw_fd();

    let mut precise = PtpSysOffsetPrecise::default();
    // SAFETY: `precise` has the exact Linux UAPI layout and remains valid for the call.
    if unsafe { ptp_sys_offset_precise(file_descriptor, &mut precise) }.is_ok()
        && valid_clock_time(&precise.device)
        && valid_clock_time(&precise.sys_realtime)
    {
        return Ok(PhcSample {
            phc_tai_nanoseconds: clock_time_to_nanoseconds(&precise.device),
            system_utc_nanoseconds: clock_time_to_nanoseconds(&precise.sys_realtime),
            sample_window_nanoseconds: None,
            method: "PTP_SYS_OFFSET_PRECISE",
        });
    }

    let mut extended = PtpSysOffsetExtended {
        n_samples: 5,
        ..PtpSysOffsetExtended::default()
    };
    // SAFETY: `extended` has the exact Linux UAPI layout and remains valid for the call.
    if unsafe { ptp_sys_offset_extended(file_descriptor, &mut extended) }.is_ok() {
        let sample_count = usize::try_from(extended.n_samples).unwrap_or(0).min(25);
        let mut best: Option<PhcSample> = None;
        for timestamps in extended.ts.iter().take(sample_count) {
            let [before, device, after] = timestamps;
            if !valid_clock_time(before) || !valid_clock_time(device) || !valid_clock_time(after) {
                continue;
            }
            let before = clock_time_to_nanoseconds(before);
            let after = clock_time_to_nanoseconds(after);
            let Some(window) = after.checked_sub(before) else {
                continue;
            };
            if !(0..=MAXIMUM_SAMPLING_WINDOW_NANOSECONDS).contains(&window) {
                continue;
            }
            if best.as_ref().is_none_or(|best| {
                best.sample_window_nanoseconds
                    .is_none_or(|value| window < value)
            }) {
                best = Some(PhcSample {
                    phc_tai_nanoseconds: clock_time_to_nanoseconds(device),
                    system_utc_nanoseconds: before + window / 2,
                    sample_window_nanoseconds: Some(window),
                    method: "PTP_SYS_OFFSET_EXTENDED",
                });
            }
        }
        if let Some(best) = best {
            return Ok(best);
        }
    }

    let mut before = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    let mut device = before;
    let mut after = before;
    let phc_clock_id = file_descriptor_clock_id(file_descriptor);
    // SAFETY: all pointers refer to initialized writable `timespec` values.
    let sampled = unsafe {
        libc::clock_gettime(libc::CLOCK_REALTIME, &mut before) == 0
            && libc::clock_gettime(phc_clock_id, &mut device) == 0
            && libc::clock_gettime(libc::CLOCK_REALTIME, &mut after) == 0
    };
    if sampled && valid_timespec(&before) && valid_timespec(&device) && valid_timespec(&after) {
        let before_nanoseconds = timespec_to_nanoseconds(&before);
        let after_nanoseconds = timespec_to_nanoseconds(&after);
        if let Some(window) = after_nanoseconds.checked_sub(before_nanoseconds)
            && (0..=MAXIMUM_SAMPLING_WINDOW_NANOSECONDS).contains(&window)
        {
            return Ok(PhcSample {
                phc_tai_nanoseconds: timespec_to_nanoseconds(&device),
                system_utc_nanoseconds: before_nanoseconds + window / 2,
                sample_window_nanoseconds: Some(window),
                method: "Bracketed clock_gettime",
            });
        }
    }

    Err(io::Error::other(
        "the PTP clock did not return a valid timestamp sample",
    ))
}

#[cfg(not(target_os = "linux"))]
pub(super) fn sample(_path: &Path) -> io::Result<PhcSample> {
    Err(io::Error::new(
        io::ErrorKind::Unsupported,
        "PHC sampling is available only on Linux",
    ))
}

#[cfg(target_os = "linux")]
#[must_use]
pub(super) fn kernel_tai_offset() -> Option<i32> {
    // SAFETY: zero is a valid initialization for the Linux `timex` input structure.
    let mut state = unsafe { std::mem::zeroed::<libc::timex>() };
    // SAFETY: `state` is a valid, writable `timex` structure for the duration of the call.
    let result = unsafe { libc::adjtimex(&mut state) };
    (result >= 0 && state.tai > 0).then_some(state.tai)
}

#[cfg(not(target_os = "linux"))]
#[must_use]
pub(super) const fn kernel_tai_offset() -> Option<i32> {
    None
}

#[cfg(target_os = "linux")]
fn valid_clock_time(value: &PtpClockTime) -> bool {
    value.sec > 0
        && value.sec <= i64::MAX / NANOSECONDS_PER_SECOND
        && value.nsec < NANOSECONDS_PER_SECOND as u32
}

#[cfg(target_os = "linux")]
fn clock_time_to_nanoseconds(value: &PtpClockTime) -> i64 {
    value.sec * NANOSECONDS_PER_SECOND + i64::from(value.nsec)
}

#[cfg(target_os = "linux")]
fn valid_timespec(value: &libc::timespec) -> bool {
    value.tv_sec > 0
        && value.tv_sec <= i64::MAX / NANOSECONDS_PER_SECOND
        && (0..NANOSECONDS_PER_SECOND).contains(&value.tv_nsec)
}

#[cfg(target_os = "linux")]
fn timespec_to_nanoseconds(value: &libc::timespec) -> i64 {
    value.tv_sec * NANOSECONDS_PER_SECOND + value.tv_nsec
}

#[cfg(target_os = "linux")]
const fn file_descriptor_clock_id(file_descriptor: libc::c_int) -> libc::clockid_t {
    ((!file_descriptor) << 3) | 3
}

#[cfg(all(test, target_os = "linux"))]
mod tests {
    use super::*;

    #[test]
    fn uapi_layouts_match_linux_header() {
        assert_eq!(std::mem::size_of::<PtpClockTime>(), 16);
        assert_eq!(std::mem::size_of::<PtpSysOffsetPrecise>(), 64);
        assert_eq!(std::mem::size_of::<PtpSysOffsetExtended>(), 1216);
    }
}
