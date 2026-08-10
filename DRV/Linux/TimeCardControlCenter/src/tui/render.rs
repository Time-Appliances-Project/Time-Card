use ratatui::Frame;
use ratatui::style::{Color, Modifier, Style};
use ratatui::text::{Line, Text};
use ratatui::widgets::Paragraph;
use unicode_width::{UnicodeWidthChar, UnicodeWidthStr};

use crate::TimeCardSnapshot;
use crate::oscillatord::OscillatordTelemetry;
use crate::timing::{format_duration, format_timestamp};

use super::TuiPage;
use super::state::TuiState;

const MINIMUM_WIDTH: usize = 48;
const MINIMUM_HEIGHT: usize = 12;
const FIXED_HEADER_LINES: usize = 4;
const FOOTER_LINES: usize = 1;
const PLAIN_WIDTH: usize = 120;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum Tone {
    Normal,
    Accent,
    Good,
    Warning,
    Error,
    Dim,
    Heading,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct StyledLine {
    text: String,
    tone: Tone,
}

impl StyledLine {
    fn new(text: impl Into<String>, tone: Tone) -> Self {
        Self {
            text: text.into(),
            tone,
        }
    }
}

#[derive(Debug)]
struct RenderedFrame {
    lines: Vec<StyledLine>,
    maximum_scroll: usize,
    page_step: usize,
}

pub(super) fn draw(frame: &mut Frame<'_>, state: &mut TuiState) {
    let area = frame.area();
    let rendered = compose_frame(state, usize::from(area.width), usize::from(area.height));
    state.set_render_metrics(rendered.maximum_scroll, rendered.page_step);
    let lines = rendered
        .lines
        .into_iter()
        .map(|line| Line::styled(line.text, style(line.tone)))
        .collect::<Vec<_>>();
    frame.render_widget(Paragraph::new(Text::from(lines)), area);
}

#[must_use]
pub(super) fn plain_text(state: &TuiState) -> String {
    let rendered = compose_frame(state, PLAIN_WIDTH, 0);
    let mut output = rendered
        .lines
        .into_iter()
        .map(|line| line.text)
        .collect::<Vec<_>>()
        .join("\n");
    output.push('\n');
    output
}

fn compose_frame(state: &TuiState, width: usize, height: usize) -> RenderedFrame {
    if height > 0 && (width < MINIMUM_WIDTH || height < MINIMUM_HEIGHT) {
        return small_terminal_frame(width, height);
    }

    let mut header = header_lines(state)
        .into_iter()
        .map(|line| StyledLine::new(clip_text(&line.text, width), line.tone))
        .collect::<Vec<_>>();
    while header.len() < FIXED_HEADER_LINES {
        header.push(StyledLine::new("", Tone::Normal));
    }

    let body = wrap_lines(body_lines(state), width);
    if height == 0 {
        let mut lines = header;
        lines.extend(body);
        lines.extend(wrap_lines(
            vec![StyledLine::new(
                format!("{} | plain snapshot", state.status_message),
                Tone::Dim,
            )],
            width,
        ));
        return RenderedFrame {
            lines,
            maximum_scroll: 0,
            page_step: 1,
        };
    }

    let body_height = height.saturating_sub(FIXED_HEADER_LINES + FOOTER_LINES);
    let maximum_scroll = body.len().saturating_sub(body_height);
    let scroll = state.scroll.min(maximum_scroll);
    let mut lines = header;
    lines.extend(body.iter().skip(scroll).take(body_height).cloned());
    while lines.len() < height.saturating_sub(FOOTER_LINES) {
        lines.push(StyledLine::new("", Tone::Normal));
    }
    let footer = if maximum_scroll > 0 {
        format!(
            "{} | lines {}-{} of {} | PgUp/PgDn scroll | ? help | q quit",
            state.status_message,
            scroll.saturating_add(1),
            (scroll + body_height).min(body.len()),
            body.len()
        )
    } else {
        format!(
            "{} | arrows/1-5 pages | j/k cards | ? help | q quit",
            state.status_message
        )
    };
    lines.push(StyledLine::new(clip_text(&footer, width), Tone::Dim));
    lines.truncate(height);

    RenderedFrame {
        lines,
        maximum_scroll,
        page_step: body_height.max(1),
    }
}

fn small_terminal_frame(width: usize, height: usize) -> RenderedFrame {
    let message =
        format!("Terminal too small: resize to at least {MINIMUM_WIDTH}x{MINIMUM_HEIGHT}");
    let mut lines = wrap_lines(vec![StyledLine::new(message, Tone::Warning)], width);
    lines.truncate(height);
    while lines.len() < height {
        lines.push(StyledLine::new("", Tone::Normal));
    }
    RenderedFrame {
        lines,
        maximum_scroll: 0,
        page_step: 1,
    }
}

fn header_lines(state: &TuiState) -> Vec<StyledLine> {
    let updated = state.snapshot_updated_at.map_or_else(
        || "waiting".to_owned(),
        |time| time.format("%H:%M:%S UTC").to_string(),
    );
    let busy = match (state.card_busy, state.oscillatord_busy) {
        (true, true) => " | polling card + oscillatord",
        (true, false) => " | polling card",
        (false, true) => " | polling oscillatord",
        (false, false) => "",
    };

    let navigation = TuiPage::ALL
        .iter()
        .map(|page| {
            let label = page.number().map_or_else(
                || page.title().to_owned(),
                |number| format!("{number} {}", page.title()),
            );
            if *page == state.page {
                format!("[{label}]")
            } else {
                label
            }
        })
        .collect::<Vec<_>>()
        .join("  ");

    let cards = if state.snapshot.available_devices.is_empty() {
        "Cards: none discovered".to_owned()
    } else {
        let values = state
            .snapshot
            .available_devices
            .iter()
            .map(|device| {
                let selected = device == &state.snapshot.device_id;
                let cursor = state.cursor_device.as_ref() == Some(device);
                match (selected, cursor) {
                    (true, true) => format!(">*{device}<"),
                    (true, false) => format!("*{device}"),
                    (false, true) => format!(">{device}<"),
                    (false, false) => device.clone(),
                }
            })
            .collect::<Vec<_>>()
            .join("  ");
        format!("Cards: {values}  (j/k move, Enter select)")
    };

    let (connection, tone) = connection_status(state);
    vec![
        StyledLine::new(
            format!(
                "TIME CARD CONTROL CENTER | {} | updated {updated}{busy}",
                state.page.title()
            ),
            Tone::Heading,
        ),
        StyledLine::new(navigation, Tone::Accent),
        StyledLine::new(cards, Tone::Normal),
        StyledLine::new(
            format!(
                "Status: {connection} | backend: {}",
                display_value(&state.snapshot.backend_name)
            ),
            tone,
        ),
    ]
}

fn connection_status(state: &TuiState) -> (&'static str, Tone) {
    if !state.snapshot_ready {
        ("LOADING", Tone::Accent)
    } else if !state.snapshot.connected {
        ("NOT DISCOVERED", Tone::Error)
    } else if !state.snapshot.errors.is_empty() {
        ("DEGRADED", Tone::Error)
    } else if state.snapshot.ptp_device.is_empty() {
        ("DISCOVERED", Tone::Warning)
    } else if state.snapshot.offset_nanoseconds.is_none() {
        ("TIMING LIMITED", Tone::Warning)
    } else {
        ("READY", Tone::Good)
    }
}

fn body_lines(state: &TuiState) -> Vec<StyledLine> {
    match state.page {
        TuiPage::Overview => overview_lines(state),
        TuiPage::TimingIo => timing_lines(&state.snapshot),
        TuiPage::Sensors => sensor_lines(&state.snapshot),
        TuiPage::Gnss => gnss_lines(&state.snapshot),
        TuiPage::Oscillatord => oscillatord_lines(state),
        TuiPage::Help => help_lines(),
    }
}

fn overview_lines(state: &TuiState) -> Vec<StyledLine> {
    let snapshot = &state.snapshot;
    let mut lines = vec![section("OVERVIEW"), section("Hardware identity")];
    fields(
        &mut lines,
        [
            ("Device", display_value(&snapshot.device_id)),
            ("Serial", display_value(&snapshot.serial_number)),
            ("Board profile", display_value(&snapshot.board_profile)),
            ("Clock source", display_value(&snapshot.clock_source)),
            ("GNSS state", display_value(&snapshot.gnss_state)),
            ("PCI address", display_value(&snapshot.pci_address)),
            ("PCI ID", pci_identity(snapshot)),
            ("sysfs", display_value(&snapshot.sysfs_path)),
        ],
    );
    lines.push(section("Precision timing"));
    fields(
        &mut lines,
        [
            ("PHC UTC", format_timestamp(snapshot.phc_utc_nanoseconds)),
            (
                "System UTC",
                format_system_timestamp(snapshot.system_utc_nanoseconds),
            ),
            (
                "PHC offset",
                format_duration(snapshot.offset_nanoseconds, true),
            ),
            (
                "Sample window",
                format_duration(snapshot.sample_window_nanoseconds, false),
            ),
            ("Clock drift", format_ppb(snapshot.clock_drift_ppb)),
            (
                "Card clock offset",
                format_duration(snapshot.clock_offset_nanoseconds, true),
            ),
            ("Clock source", display_value(&snapshot.clock_source)),
            ("UTC-TAI", utc_tai(snapshot)),
            ("Sampling", display_value(&snapshot.timestamp_method)),
        ],
    );
    inventory(&mut lines, "Capabilities", &snapshot.capabilities);
    lines.push(section("Diagnostics"));
    if snapshot.errors.is_empty() {
        lines.push(StyledLine::new("  No card telemetry errors", Tone::Good));
    } else {
        for error in &snapshot.errors {
            push_multiline(&mut lines, "  ! ", error, Tone::Warning);
        }
    }
    lines.push(StyledLine::new(
        format!(
            "  Session log: {} retained, {} dropped",
            state.log.len(),
            state.log.dropped_record_count()
        ),
        Tone::Dim,
    ));
    for record in state.log.text_lines(Some(3)) {
        lines.push(StyledLine::new(format!("  {record}"), Tone::Dim));
    }
    lines
}

fn timing_lines(snapshot: &TimeCardSnapshot) -> Vec<StyledLine> {
    let mut lines = vec![section("TIMING I/O"), section("Clock comparison")];
    fields(
        &mut lines,
        [
            (
                "PHC TAI",
                format_tai_timestamp(snapshot.phc_tai_nanoseconds),
            ),
            ("PHC UTC", format_timestamp(snapshot.phc_utc_nanoseconds)),
            (
                "System UTC",
                format_system_timestamp(snapshot.system_utc_nanoseconds),
            ),
            ("Offset", format_duration(snapshot.offset_nanoseconds, true)),
            (
                "Window",
                format_duration(snapshot.sample_window_nanoseconds, false),
            ),
            ("Clock drift", format_ppb(snapshot.clock_drift_ppb)),
            ("UTC-TAI", utc_tai(snapshot)),
            ("Method", display_value(&snapshot.timestamp_method)),
        ],
    );
    lines.push(section("Linux device nodes"));
    fields(
        &mut lines,
        [
            ("PTP", display_value(&snapshot.ptp_device)),
            ("PPS", display_value(&snapshot.pps_device)),
            ("I2C", display_value(&snapshot.i2c_device)),
            ("mRO-50", display_value(&snapshot.mro50_device)),
        ],
    );
    inventory(&mut lines, "SMA routing", &snapshot.sma_states);
    inventory(&mut lines, "Signal generators", &snapshot.generator_states);
    inventory(
        &mut lines,
        "Frequency counters",
        &snapshot.frequency_counter_states,
    );
    inventory(
        &mut lines,
        "FPGA timing engines",
        &snapshot.fpga_engine_states,
    );
    lines.push(section("FPGA image contract"));
    lines.push(StyledLine::new(
        format!("  {}", display_value(&snapshot.optional_image_contract)),
        Tone::Normal,
    ));
    lines
}

fn sensor_lines(snapshot: &TimeCardSnapshot) -> Vec<StyledLine> {
    let mut lines = vec![section("SENSORS"), section("Card scope")];
    fields(
        &mut lines,
        [
            ("Device", display_value(&snapshot.device_id)),
            ("Serial", display_value(&snapshot.serial_number)),
            ("Board profile", display_value(&snapshot.board_profile)),
            ("PCI address", display_value(&snapshot.pci_address)),
            (
                "Image contract",
                display_value(&snapshot.optional_image_contract),
            ),
        ],
    );
    inventory(&mut lines, "Sensor telemetry", &snapshot.sensor_states);
    inventory(&mut lines, "Status LEDs", &snapshot.led_states);
    lines
}

fn gnss_lines(snapshot: &TimeCardSnapshot) -> Vec<StyledLine> {
    let mut lines = vec![section("GNSS"), section("Supervisor")];
    fields(
        &mut lines,
        [
            ("GNSS state", display_value(&snapshot.gnss_state)),
            (
                "GNSS lock",
                if snapshot.gnss_locked {
                    "Locked"
                } else {
                    "Not locked"
                }
                .to_owned(),
            ),
            ("Clock source", display_value(&snapshot.clock_source)),
            ("ToD protocol", display_value(&snapshot.tod_protocol)),
            ("ToD baud", display_value(&snapshot.tod_baud_rate)),
        ],
    );
    lines.push(section("Serial endpoints"));
    fields(
        &mut lines,
        [
            ("GNSS primary", display_value(&snapshot.tty_gnss)),
            ("GNSS secondary", display_value(&snapshot.tty_gnss2)),
            ("MAC", display_value(&snapshot.tty_mac)),
            ("NMEA", display_value(&snapshot.tty_nmea)),
        ],
    );
    inventory(&mut lines, "Related capabilities", &snapshot.capabilities);
    lines
}

fn oscillatord_lines(state: &TuiState) -> Vec<StyledLine> {
    let mut lines = vec![section("OSCILLATORD"), section("Endpoint-scoped service")];
    fields(
        &mut lines,
        [
            ("Endpoint", state.endpoint.clone()),
            (
                "Last attempt",
                state.oscillatord_updated_at.map_or_else(
                    || "Waiting for first response".to_owned(),
                    |time| time.to_rfc3339(),
                ),
            ),
        ],
    );
    if state.oscillatord_busy && state.oscillatord.is_none() {
        lines.push(StyledLine::new("  Polling service...", Tone::Accent));
    }
    if let Some(error) = &state.oscillatord_error {
        push_multiline(&mut lines, "  ! ", error, Tone::Warning);
    }
    let Some(telemetry) = &state.oscillatord else {
        lines.push(StyledLine::new("  Telemetry unavailable", Tone::Dim));
        return lines;
    };
    append_oscillatord_telemetry(&mut lines, telemetry);
    lines
}

fn append_oscillatord_telemetry(lines: &mut Vec<StyledLine>, telemetry: &OscillatordTelemetry) {
    lines.push(section("Service and clock"));
    fields(
        lines,
        [
            (
                "Service",
                format!("oscillatord {}", display_value(&telemetry.service_version)),
            ),
            ("Status action", display_value(&telemetry.action_requested)),
            (
                "Control policy",
                if telemetry.control_enabled {
                    "Enabled"
                } else {
                    "Disabled"
                }
                .to_owned(),
            ),
            ("Clock class", display_value(&telemetry.clock_class)),
            (
                "Clock offset",
                format_duration(Some(telemetry.clock_offset_nanoseconds), true),
            ),
        ],
    );
    if let Some(discipline) = &telemetry.discipline {
        lines.push(section("Disciplining"));
        fields(
            lines,
            [
                ("Status", display_value(&discipline.status)),
                (
                    "Convergence",
                    format!("{:.1}%", discipline.convergence_progress.clamp(0.0, 100.0)),
                ),
                (
                    "Samples",
                    match (
                        discipline.current_convergence_count,
                        discipline.convergence_threshold,
                    ) {
                        (Some(current), Some(threshold)) if current >= 0 && threshold > 0 => {
                            format!("{current}/{threshold}")
                        }
                        _ => "Unavailable".to_owned(),
                    },
                ),
                (
                    "Holdover ready",
                    yes_no(discipline.ready_for_holdover).to_owned(),
                ),
            ],
        );
    }
    lines.push(section("Oscillator"));
    fields(
        lines,
        [
            ("Model", display_value(&telemetry.oscillator_model)),
            ("Lock", yes_no(telemetry.oscillator_locked).to_owned()),
            ("Fine control", format_optional(telemetry.fine_control)),
            ("Coarse control", format_optional(telemetry.coarse_control)),
            (
                "Temperature",
                if telemetry.oscillator_temperature_celsius > -273.15 {
                    format!("{:.2} C", telemetry.oscillator_temperature_celsius)
                } else {
                    "Unavailable".to_owned()
                },
            ),
        ],
    );
    lines.push(section("GNSS and antenna"));
    fields(
        lines,
        [
            ("GNSS fix", format_optional(telemetry.gnss_fix)),
            ("Fix valid", yes_no(telemetry.gnss_fix_ok).to_owned()),
            (
                "Satellites",
                if telemetry.satellites >= 0 {
                    telemetry.satellites.to_string()
                } else {
                    "Unavailable".to_owned()
                },
            ),
            ("Antenna power", format_optional(telemetry.antenna_power)),
            ("Antenna status", format_optional(telemetry.antenna_status)),
            (
                "Leap-second change",
                format_optional(telemetry.leap_second_change),
            ),
            ("Leap seconds", format_optional(telemetry.leap_seconds)),
            (
                "Survey error",
                telemetry
                    .survey_position_error_meters
                    .map_or_else(|| "Unavailable".to_owned(), |value| format!("{value:.3} m")),
            ),
            (
                "Time accuracy",
                telemetry
                    .time_accuracy_nanoseconds
                    .map_or_else(|| "Unavailable".to_owned(), |value| format!("{value} ns")),
            ),
        ],
    );
    if !telemetry.service_error.is_empty() {
        push_multiline(
            lines,
            "  Service error: ",
            &telemetry.service_error,
            Tone::Error,
        );
    }
}

fn help_lines() -> Vec<StyledLine> {
    let mut lines = vec![section("HELP"), section("Navigation")];
    fields(
        &mut lines,
        [
            ("Left / h / Shift+Tab", "Previous workspace".to_owned()),
            ("Right / l / Tab", "Next workspace".to_owned()),
            ("1 through 5", "Open a telemetry workspace".to_owned()),
            ("?", "Toggle Help and return".to_owned()),
            (
                "PageUp / PageDown",
                "Scroll the current workspace".to_owned(),
            ),
        ],
    );
    lines.push(section("Cards and telemetry"));
    fields(
        &mut lines,
        [
            ("Up / Down or k / j", "Move the card cursor".to_owned()),
            ("Enter", "Select the highlighted card".to_owned()),
            ("r", "Refresh card telemetry".to_owned()),
            ("o", "Refresh oscillatord telemetry".to_owned()),
        ],
    );
    lines.push(section("Session"));
    fields(
        &mut lines,
        [
            ("x", "Export the structured session log".to_owned()),
            ("c", "Clear the in-memory session log".to_owned()),
            (
                "q / Escape / Ctrl+C",
                "Quit and restore the terminal".to_owned(),
            ),
        ],
    );
    lines.push(section("Automation"));
    lines.push(StyledLine::new(
        "  Use --plain for one complete, ANSI-free snapshot suitable for scripts.",
        Tone::Dim,
    ));
    lines
}

fn fields<const N: usize>(lines: &mut Vec<StyledLine>, values: [(&str, String); N]) {
    for (label, value) in values {
        push_multiline(lines, &format!("  {label}: "), &value, Tone::Normal);
    }
}

fn inventory(lines: &mut Vec<StyledLine>, title: &str, values: &[String]) {
    lines.push(section(title));
    if values.is_empty() {
        lines.push(StyledLine::new("  Unavailable", Tone::Dim));
    } else {
        for value in values {
            push_multiline(lines, "  - ", value, Tone::Normal);
        }
    }
}

fn push_multiline(lines: &mut Vec<StyledLine>, prefix: &str, value: &str, tone: Tone) {
    let cleaned = value.replace('\r', "");
    let mut parts = cleaned.split('\n');
    if let Some(first) = parts.next() {
        lines.push(StyledLine::new(format!("{prefix}{first}"), tone));
    }
    let continuation = " ".repeat(UnicodeWidthStr::width(prefix));
    for part in parts {
        lines.push(StyledLine::new(format!("{continuation}{part}"), tone));
    }
}

fn section(title: &str) -> StyledLine {
    StyledLine::new(format!("\n{title}"), Tone::Accent)
}

fn display_value(value: &str) -> String {
    if value.trim().is_empty() {
        "Unavailable".to_owned()
    } else {
        value.trim().to_owned()
    }
}

fn format_optional(value: Option<i64>) -> String {
    value.map_or_else(|| "Unavailable".to_owned(), |value| value.to_string())
}

fn format_ppb(value: Option<i64>) -> String {
    value.map_or_else(
        || "Unavailable".to_owned(),
        |value| format!("{value:+} ppb"),
    )
}

fn format_tai_timestamp(value: Option<i64>) -> String {
    format_timestamp(value).replace(" UTC", " TAI")
}

fn format_system_timestamp(value: i64) -> String {
    format_timestamp((value != 0).then_some(value))
}

fn utc_tai(snapshot: &TimeCardSnapshot) -> String {
    snapshot.utc_tai_offset.map_or_else(
        || "Unavailable".to_owned(),
        |offset| {
            let source = if snapshot.utc_tai_offset_from_kernel {
                "kernel"
            } else {
                "card fallback"
            };
            format!("UTC + {offset} s ({source})")
        },
    )
}

fn pci_identity(snapshot: &TimeCardSnapshot) -> String {
    match (
        snapshot.pci_vendor.trim().is_empty(),
        snapshot.pci_device.trim().is_empty(),
    ) {
        (true, true) => "Unavailable".to_owned(),
        _ => format!(
            "{}:{}",
            display_value(&snapshot.pci_vendor),
            display_value(&snapshot.pci_device)
        ),
    }
}

const fn yes_no(value: bool) -> &'static str {
    if value { "Yes" } else { "No" }
}

fn wrap_lines(lines: Vec<StyledLine>, width: usize) -> Vec<StyledLine> {
    let mut wrapped = Vec::new();
    for line in lines {
        for text in wrap_text(&line.text, width) {
            wrapped.push(StyledLine::new(text, line.tone));
        }
    }
    wrapped
}

fn wrap_text(text: &str, width: usize) -> Vec<String> {
    if width == 0 {
        return vec![String::new()];
    }
    let mut output = Vec::new();
    for logical in text.replace('\r', "").split('\n') {
        if logical.is_empty() {
            output.push(String::new());
            continue;
        }
        let mut line = String::new();
        let mut line_width = 0;
        for character in logical.chars() {
            let character = if character == '\t' {
                ' '
            } else if character.is_control() {
                '\u{fffd}'
            } else {
                character
            };
            let character_width = UnicodeWidthChar::width(character).unwrap_or(0);
            if character_width > width {
                continue;
            }
            if line_width + character_width > width && !line.is_empty() {
                output.push(line);
                line = String::new();
                line_width = 0;
            }
            line.push(character);
            line_width += character_width;
        }
        output.push(line);
    }
    output
}

fn clip_text(text: &str, width: usize) -> String {
    wrap_text(text, width)
        .into_iter()
        .next()
        .unwrap_or_default()
}

fn style(tone: Tone) -> Style {
    match tone {
        Tone::Normal => Style::default().fg(Color::Reset),
        Tone::Accent => Style::default().fg(Color::Cyan),
        Tone::Good => Style::default()
            .fg(Color::Green)
            .add_modifier(Modifier::BOLD),
        Tone::Warning => Style::default()
            .fg(Color::Yellow)
            .add_modifier(Modifier::BOLD),
        Tone::Error => Style::default().fg(Color::Red).add_modifier(Modifier::BOLD),
        Tone::Dim => Style::default()
            .fg(Color::Reset)
            .add_modifier(Modifier::DIM),
        Tone::Heading => Style::default()
            .fg(Color::Cyan)
            .add_modifier(Modifier::BOLD),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::TimeCardBackend;
    use crate::mock::MockTimeCardBackend;

    fn populated_state(page: TuiPage) -> TuiState {
        let mut backend = MockTimeCardBackend::new();
        let mut state = TuiState::new(page, "127.0.0.1:2958".to_owned());
        state.apply_snapshot(backend.read_snapshot());
        state
    }

    #[test]
    fn every_workspace_renders_its_title() {
        for page in TuiPage::ALL {
            let state = populated_state(page);
            let text = plain_text(&state);
            assert!(text.contains(page.title()), "missing {:?} in {text}", page);
        }
    }

    #[test]
    fn interactive_frame_is_bounded_exactly() {
        let state = populated_state(TuiPage::TimingIo);
        let rendered = compose_frame(&state, 80, 24);
        assert_eq!(rendered.lines.len(), 24);
        assert!(
            rendered
                .lines
                .iter()
                .all(|line| UnicodeWidthStr::width(line.text.as_str()) <= 80)
        );
    }

    #[test]
    fn selected_and_cursor_cards_have_distinct_markers() {
        let mut state = populated_state(TuiPage::Overview);
        state.cursor_device = Some("mock1".to_owned());
        let text = plain_text(&state);
        assert!(text.contains("*mock0"));
        assert!(text.contains(">mock1<"));
    }

    #[test]
    fn tiny_terminals_show_resize_message() {
        let state = populated_state(TuiPage::Overview);
        let rendered = compose_frame(&state, 40, 8);
        assert_eq!(rendered.lines.len(), 8);
        assert!(rendered.lines[0].text.contains("Terminal too small"));
    }

    #[test]
    fn plain_output_is_complete_and_ansi_free() {
        let state = populated_state(TuiPage::TimingIo);
        let text = plain_text(&state);
        assert!(text.contains("IRIG/DCF"));
        assert!(!text.contains('\u{1b}'));
        assert!(
            text.lines()
                .all(|line| UnicodeWidthStr::width(line) <= PLAIN_WIDTH)
        );
        assert!(text.ends_with('\n'));
    }

    #[test]
    fn hostile_values_cannot_emit_terminal_controls() {
        let mut state = populated_state(TuiPage::Overview);
        state.endpoint = format!("{}\u{1b}[31m\u{7}\t", "endpoint".repeat(40));
        state.snapshot.device_id = "mock\u{1b}[2J\t0".to_owned();
        state.snapshot.available_devices = vec![state.snapshot.device_id.clone()];
        state.snapshot.errors = vec!["bad\u{7}value\u{9b}31m".to_owned()];
        state.status_message = "status\u{1b}[5n\u{7}".repeat(30);
        let text = plain_text(&state);
        assert!(
            text.chars()
                .all(|character| character == '\n' || !character.is_control())
        );
        assert!(
            text.lines()
                .all(|line| UnicodeWidthStr::width(line) <= PLAIN_WIDTH)
        );
    }

    #[test]
    fn hardware_errors_override_ready_status() {
        let mut state = populated_state(TuiPage::Overview);
        state.snapshot.errors = vec!["FPGA image mismatch".to_owned()];
        assert_eq!(connection_status(&state), ("DEGRADED", Tone::Error));
    }

    #[test]
    fn multiline_errors_become_separate_physical_lines() {
        let mut state = populated_state(TuiPage::Overview);
        state.snapshot.errors = vec!["first line\r\nsecond line".to_owned()];
        let rendered = compose_frame(&state, 80, 0);
        assert!(
            rendered
                .lines
                .iter()
                .any(|line| line.text.contains("first line"))
        );
        assert!(
            rendered
                .lines
                .iter()
                .any(|line| line.text.contains("second line"))
        );
        assert!(
            rendered
                .lines
                .iter()
                .all(|line| !line.text.contains('\r') && !line.text.contains('\n'))
        );
    }

    #[test]
    fn scrolling_can_reach_deep_timing_inventory() {
        let mut state = populated_state(TuiPage::TimingIo);
        let initial = compose_frame(&state, 80, 24);
        state.scroll = initial.maximum_scroll;
        let final_frame = compose_frame(&state, 80, 24);
        assert!(
            final_frame
                .lines
                .iter()
                .any(|line| line.text.contains("IRIG/DCF"))
        );
    }

    #[test]
    fn oscillatord_progress_is_clamped_for_display() {
        let mut state = populated_state(TuiPage::Oscillatord);
        state.oscillatord = Some(OscillatordTelemetry {
            service_version: "9.9.0".to_owned(),
            discipline: Some(crate::oscillatord::DisciplineTelemetry {
                convergence_progress: 140.0,
                ..crate::oscillatord::DisciplineTelemetry::default()
            }),
            ..OscillatordTelemetry::default()
        });
        let text = plain_text(&state);
        assert!(text.contains("oscillatord 9.9.0"));
        assert!(text.contains("100.0%"));
    }

    #[test]
    fn unicode_wrapping_respects_display_width() {
        for width in 1..20 {
            for line in wrap_text("時間 card telemetry", width) {
                assert!(UnicodeWidthStr::width(line.as_str()) <= width);
            }
        }
    }
}
