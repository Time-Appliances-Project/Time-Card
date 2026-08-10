use chrono::{DateTime, Utc};
use crossterm::event::{KeyCode, KeyEvent, KeyEventKind, KeyModifiers};

use crate::TimeCardSnapshot;
use crate::oscillatord::OscillatordTelemetry;
use crate::session_log::{SessionLogSeverity, SessionLogStore};

use super::TuiPage;

#[derive(Clone, Debug, Eq, PartialEq)]
pub(super) enum TuiAction {
    None,
    Quit,
    RefreshCard,
    RefreshOscillatord,
    SelectDevice(String),
    ExportLog,
    ClearLog,
}

#[derive(Debug)]
pub(super) struct TuiState {
    pub page: TuiPage,
    pub previous_page: TuiPage,
    pub snapshot: TimeCardSnapshot,
    pub snapshot_ready: bool,
    pub snapshot_updated_at: Option<DateTime<Utc>>,
    pub oscillatord: Option<OscillatordTelemetry>,
    pub oscillatord_error: Option<String>,
    pub oscillatord_updated_at: Option<DateTime<Utc>>,
    pub endpoint: String,
    pub cursor_device: Option<String>,
    pub scroll: usize,
    pub maximum_scroll: usize,
    pub page_step: usize,
    pub card_busy: bool,
    pub oscillatord_busy: bool,
    pub status_message: String,
    pub log: SessionLogStore,
}

impl TuiState {
    pub fn new(page: TuiPage, endpoint: String) -> Self {
        Self {
            page,
            previous_page: if page == TuiPage::Help {
                TuiPage::Overview
            } else {
                page
            },
            snapshot: TimeCardSnapshot::default(),
            snapshot_ready: false,
            snapshot_updated_at: None,
            oscillatord: None,
            oscillatord_error: None,
            oscillatord_updated_at: None,
            endpoint,
            cursor_device: None,
            scroll: 0,
            maximum_scroll: 0,
            page_step: 1,
            card_busy: false,
            oscillatord_busy: false,
            status_message: "Starting telemetry workers".to_owned(),
            log: SessionLogStore::default(),
        }
    }

    pub fn apply_snapshot(&mut self, snapshot: TimeCardSnapshot) {
        let previous_device = self.snapshot.device_id.clone();
        let previous_errors = self.snapshot.errors.clone();
        let selected = snapshot.device_id.clone();
        let devices = &snapshot.available_devices;
        if self
            .cursor_device
            .as_ref()
            .is_none_or(|cursor| !devices.contains(cursor))
        {
            self.cursor_device = devices
                .iter()
                .find(|device| **device == selected)
                .or_else(|| devices.first())
                .cloned();
        }

        if previous_device != selected && !selected.is_empty() {
            self.log.append(
                SessionLogSeverity::Info,
                "Card",
                &selected,
                &format!("Selected {selected}"),
            );
        }
        for error in snapshot
            .errors
            .iter()
            .filter(|error| !previous_errors.contains(error))
        {
            self.log.append(
                SessionLogSeverity::Warn,
                "Telemetry",
                card_context(&snapshot),
                error,
            );
        }

        self.status_message = if snapshot.connected {
            format!("Telemetry updated for {}", card_context(&snapshot))
        } else {
            "No Time Card discovered".to_owned()
        };
        self.snapshot = snapshot;
        self.snapshot_ready = true;
        self.snapshot_updated_at = Some(Utc::now());
        self.card_busy = false;
    }

    pub fn apply_oscillatord(&mut self, result: Result<OscillatordTelemetry, String>) {
        self.oscillatord_updated_at = Some(Utc::now());
        self.oscillatord_busy = false;
        match result {
            Ok(telemetry) => {
                let had_error = self.oscillatord_error.take();
                if had_error.is_some() {
                    self.log.append(
                        SessionLogSeverity::Info,
                        "oscillatord",
                        &format!("oscillatord@{}", self.endpoint),
                        "Monitoring connection restored",
                    );
                }
                self.status_message = format!("oscillatord updated from {}", self.endpoint);
                self.oscillatord = Some(telemetry);
            }
            Err(error) => {
                if self.oscillatord_error.as_ref() != Some(&error) {
                    self.log.append(
                        SessionLogSeverity::Warn,
                        "oscillatord",
                        &format!("oscillatord@{}", self.endpoint),
                        &error,
                    );
                }
                self.status_message = format!("oscillatord unavailable at {}", self.endpoint);
                self.oscillatord_error = Some(error);
                self.oscillatord = None;
            }
        }
    }

    pub fn set_render_metrics(&mut self, maximum_scroll: usize, page_step: usize) {
        self.maximum_scroll = maximum_scroll;
        self.page_step = page_step.max(1);
        self.scroll = self.scroll.min(self.maximum_scroll);
    }

    pub fn handle_key(&mut self, key: KeyEvent) -> TuiAction {
        if key.kind == KeyEventKind::Release {
            return TuiAction::None;
        }
        if key.modifiers.contains(KeyModifiers::CONTROL)
            && matches!(key.code, KeyCode::Char('c' | 'C'))
        {
            return TuiAction::Quit;
        }

        match key.code {
            KeyCode::Esc | KeyCode::Char('q' | 'Q') => TuiAction::Quit,
            KeyCode::Left | KeyCode::BackTab | KeyCode::Char('h' | 'H') => {
                self.set_page(self.page.previous());
                TuiAction::None
            }
            KeyCode::Right | KeyCode::Tab | KeyCode::Char('l' | 'L') => {
                self.set_page(self.page.next());
                TuiAction::None
            }
            KeyCode::Char('?') => {
                if self.page == TuiPage::Help {
                    self.set_page(self.previous_page);
                } else {
                    self.previous_page = self.page;
                    self.set_page(TuiPage::Help);
                }
                TuiAction::None
            }
            KeyCode::Char('1') => self.open_numbered_page(TuiPage::Overview),
            KeyCode::Char('2') => self.open_numbered_page(TuiPage::TimingIo),
            KeyCode::Char('3') => self.open_numbered_page(TuiPage::Sensors),
            KeyCode::Char('4') => self.open_numbered_page(TuiPage::Gnss),
            KeyCode::Char('5') => self.open_numbered_page(TuiPage::Oscillatord),
            KeyCode::Up | KeyCode::Char('k' | 'K') => {
                self.move_cursor(-1);
                TuiAction::None
            }
            KeyCode::Down | KeyCode::Char('j' | 'J') => {
                self.move_cursor(1);
                TuiAction::None
            }
            KeyCode::Enter => self
                .cursor_device
                .clone()
                .map_or(TuiAction::None, TuiAction::SelectDevice),
            KeyCode::PageUp => {
                self.scroll = self.scroll.saturating_sub(self.page_step);
                TuiAction::None
            }
            KeyCode::PageDown => {
                self.scroll = self
                    .scroll
                    .saturating_add(self.page_step)
                    .min(self.maximum_scroll);
                TuiAction::None
            }
            KeyCode::Char('r' | 'R') => TuiAction::RefreshCard,
            KeyCode::Char('o' | 'O') => TuiAction::RefreshOscillatord,
            KeyCode::Char('x' | 'X') => TuiAction::ExportLog,
            KeyCode::Char('c' | 'C') => TuiAction::ClearLog,
            _ => TuiAction::None,
        }
    }

    pub fn clear_log(&mut self) {
        self.log.clear();
        self.status_message = "Session log cleared".to_owned();
    }

    pub fn note_export(&mut self, result: Result<&str, &str>) {
        match result {
            Ok(path) => {
                self.status_message = format!("Session log exported to {path}");
                self.log.append(
                    SessionLogSeverity::Info,
                    "Session",
                    card_context(&self.snapshot),
                    &self.status_message,
                );
            }
            Err(error) => {
                self.status_message = format!("Session export failed: {error}");
                self.log.append(
                    SessionLogSeverity::Error,
                    "Session",
                    card_context(&self.snapshot),
                    &self.status_message,
                );
            }
        }
    }

    fn open_numbered_page(&mut self, page: TuiPage) -> TuiAction {
        self.set_page(page);
        TuiAction::None
    }

    fn set_page(&mut self, page: TuiPage) {
        if page != TuiPage::Help {
            self.previous_page = page;
        }
        self.page = page;
        self.scroll = 0;
        self.maximum_scroll = 0;
    }

    fn move_cursor(&mut self, direction: isize) {
        let devices = &self.snapshot.available_devices;
        if devices.is_empty() {
            self.cursor_device = None;
            return;
        }
        let current = self
            .cursor_device
            .as_ref()
            .and_then(|cursor| devices.iter().position(|device| device == cursor))
            .unwrap_or(0);
        let length = devices.len() as isize;
        let next = (current as isize + direction).rem_euclid(length) as usize;
        self.cursor_device = Some(devices[next].clone());
    }
}

fn card_context(snapshot: &TimeCardSnapshot) -> &str {
    if snapshot.device_id.is_empty() {
        "offline"
    } else {
        &snapshot.device_id
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crossterm::event::KeyEventState;

    fn state_with_cards() -> TuiState {
        let mut state = TuiState::new(TuiPage::Overview, "127.0.0.1:2958".to_owned());
        state.apply_snapshot(TimeCardSnapshot {
            connected: true,
            device_id: "mock0".to_owned(),
            available_devices: vec!["mock0".to_owned(), "mock1".to_owned()],
            ..TimeCardSnapshot::default()
        });
        state
    }

    fn key(code: KeyCode) -> KeyEvent {
        KeyEvent {
            code,
            modifiers: KeyModifiers::NONE,
            kind: KeyEventKind::Press,
            state: KeyEventState::NONE,
        }
    }

    #[test]
    fn moving_cursor_does_not_select_hardware() {
        let mut state = state_with_cards();
        assert_eq!(state.handle_key(key(KeyCode::Down)), TuiAction::None);
        assert_eq!(state.snapshot.device_id, "mock0");
        assert_eq!(state.cursor_device.as_deref(), Some("mock1"));
        assert_eq!(
            state.handle_key(key(KeyCode::Enter)),
            TuiAction::SelectDevice("mock1".to_owned())
        );
    }

    #[test]
    fn help_returns_to_previous_page() {
        let mut state = state_with_cards();
        state.handle_key(key(KeyCode::Char('3')));
        state.handle_key(key(KeyCode::Char('?')));
        assert_eq!(state.page, TuiPage::Help);
        state.handle_key(key(KeyCode::Char('?')));
        assert_eq!(state.page, TuiPage::Sensors);
    }

    #[test]
    fn page_down_clamps_to_rendered_extent() {
        let mut state = state_with_cards();
        state.set_render_metrics(20, 7);
        for _ in 0..10 {
            state.handle_key(key(KeyCode::PageDown));
        }
        assert_eq!(state.scroll, 20);
    }

    #[test]
    fn release_events_are_ignored() {
        let mut state = state_with_cards();
        let mut event = key(KeyCode::Char('q'));
        event.kind = KeyEventKind::Release;
        assert_eq!(state.handle_key(event), TuiAction::None);
    }
}
