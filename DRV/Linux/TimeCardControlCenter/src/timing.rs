use chrono::{DateTime, Utc};

pub const NANOSECONDS_PER_SECOND: i64 = 1_000_000_000;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TaiOffsetSource {
    Kernel,
    TimeCard,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TaiOffset {
    pub seconds: i32,
    pub source: TaiOffsetSource,
}

#[must_use]
pub fn select_tai_offset(kernel: Option<i32>, card: Option<i32>) -> Option<TaiOffset> {
    const MAXIMUM_PLAUSIBLE_OFFSET: i32 = 255;
    let valid = |seconds: i32| (1..=MAXIMUM_PLAUSIBLE_OFFSET).contains(&seconds);

    if let Some(seconds) = kernel.filter(|seconds| valid(*seconds)) {
        return Some(TaiOffset {
            seconds,
            source: TaiOffsetSource::Kernel,
        });
    }
    card.filter(|seconds| valid(*seconds))
        .map(|seconds| TaiOffset {
            seconds,
            source: TaiOffsetSource::TimeCard,
        })
}

#[must_use]
pub fn derive_tai_aware_timing(
    phc_tai_nanoseconds: i64,
    system_utc_nanoseconds: i64,
    utc_tai_offset_seconds: i32,
) -> Option<(i64, i64)> {
    let tai_offset = i64::from(utc_tai_offset_seconds).checked_mul(NANOSECONDS_PER_SECOND)?;
    let phc_utc = phc_tai_nanoseconds.checked_sub(tai_offset)?;
    let system_tai = system_utc_nanoseconds.checked_add(tai_offset)?;
    let offset = phc_tai_nanoseconds.checked_sub(system_tai)?;
    Some((phc_utc, offset))
}

#[must_use]
pub fn format_timestamp(nanoseconds: Option<i64>) -> String {
    let Some(nanoseconds) = nanoseconds else {
        return "Unavailable".to_owned();
    };
    let seconds = nanoseconds.div_euclid(NANOSECONDS_PER_SECOND);
    let remainder = nanoseconds.rem_euclid(NANOSECONDS_PER_SECOND) as u32;
    DateTime::<Utc>::from_timestamp(seconds, remainder).map_or_else(
        || "Unavailable".to_owned(),
        |value| value.format("%Y-%m-%d  %H:%M:%S.%9f UTC").to_string(),
    )
}

#[must_use]
pub fn format_duration(nanoseconds: Option<i64>, force_sign: bool) -> String {
    let Some(nanoseconds) = nanoseconds else {
        return "Unavailable".to_owned();
    };
    let absolute = (nanoseconds as f64).abs();
    let sign = if force_sign && nanoseconds >= 0 {
        "+"
    } else {
        ""
    };
    if absolute >= 1_000_000_000.0 {
        format!("{sign}{:.6} s", nanoseconds as f64 / 1.0e9)
    } else if absolute >= 1_000_000.0 {
        format!("{sign}{:.3} ms", nanoseconds as f64 / 1.0e6)
    } else if absolute >= 1_000.0 {
        format!("{sign}{:.3} us", nanoseconds as f64 / 1.0e3)
    } else {
        format!("{sign}{nanoseconds} ns")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn kernel_tai_offset_wins_over_card_value() {
        assert_eq!(
            select_tai_offset(Some(37), Some(36)),
            Some(TaiOffset {
                seconds: 37,
                source: TaiOffsetSource::Kernel
            })
        );
    }

    #[test]
    fn implausible_offsets_are_rejected() {
        assert_eq!(select_tai_offset(Some(0), Some(256)), None);
    }

    #[test]
    fn tai_aware_offset_does_not_include_leap_seconds() {
        let system_utc = 1_700_000_000 * NANOSECONDS_PER_SECOND;
        let phc_tai = system_utc + 37 * NANOSECONDS_PER_SECOND + 42;
        assert_eq!(
            derive_tai_aware_timing(phc_tai, system_utc, 37),
            Some((system_utc + 42, 42))
        );
    }

    #[test]
    fn durations_choose_a_readable_unit() {
        assert_eq!(format_duration(Some(42), true), "+42 ns");
        assert_eq!(format_duration(Some(-1_250), true), "-1.250 us");
        assert_eq!(format_duration(None, true), "Unavailable");
    }
}
