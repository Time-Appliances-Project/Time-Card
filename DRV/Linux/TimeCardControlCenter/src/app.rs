use std::cell::{Cell, RefCell};
use std::collections::HashMap;
use std::path::PathBuf;
use std::rc::Rc;
use std::time::Duration;

use chrono::{Local, Utc};
use directories::UserDirs;
use relm4::adw;
use relm4::adw::prelude::*;
use relm4::gtk;
use relm4::gtk::glib;
use relm4::{
    Component, ComponentParts, ComponentSender, SimpleComponent, Worker, WorkerController,
};

use crate::oscillatord::{OscillatordTelemetry, display_endpoint};
use crate::session_log::{SessionLogSeverity, SessionLogStore};
use crate::timing::{format_duration, format_timestamp};
use crate::{
    AppConfig, LinuxTimeCardBackend, MockTimeCardBackend, Page, TimeCardBackend, TimeCardSnapshot,
};

const APP_TITLE: &str = "OCP Time Card Control Center";
const POLL_INTERVAL: Duration = Duration::from_secs(1);
const OSCILLATORD_TIMEOUT: Duration = Duration::from_secs(1);
const OSCILLATORD_POLL_TICKS: u8 = 5;
const SESSION_LOG_CAPACITY: usize = 500;
const OFFSET_HISTORY_CAPACITY: usize = 200;
const WINDOW_HISTORY_CAPACITY: usize = 60;

pub const APP_CSS: &str = r"
window.timecard-window {
  background: #091319;
  color: #e9f3f4;
}

headerbar.timecard-header {
  background: #0c1a21;
  border-bottom: 1px solid rgba(117, 184, 196, 0.18);
  box-shadow: none;
  min-height: 64px;
}

.header-title {
  font-size: 18px;
  font-weight: 700;
}

.header-subtitle, .muted {
  color: #829ca6;
}

.sidebar {
  background: #0b1a21;
  border-right: 1px solid rgba(117, 184, 196, 0.16);
  padding: 18px;
}

.brand-mark {
  background: #14383b;
  border: 1px solid #276266;
  border-radius: 12px;
  color: #6ff0e5;
  font-size: 14px;
  font-weight: 800;
  min-height: 42px;
  min-width: 42px;
}

.brand-title {
  font-size: 14px;
  font-weight: 800;
  letter-spacing: 1.2px;
}

.eyebrow {
  color: #718f9a;
  font-size: 10px;
  font-weight: 700;
  letter-spacing: 1.4px;
}

button.nav-button {
  background: transparent;
  border: 0;
  border-radius: 10px;
  box-shadow: none;
  color: #9ab1b9;
  min-height: 42px;
  padding: 0 12px;
}

button.nav-button:hover {
  background: rgba(82, 163, 170, 0.10);
}

button.nav-button.selected {
  background: #15343b;
  color: #72e6dc;
}

.connection-card {
  background: #10242a;
  border: 1px solid rgba(105, 190, 178, 0.23);
  border-radius: 12px;
  padding: 12px;
}

.connection-badge {
  color: #e2bd76;
  font-size: 11px;
  font-weight: 800;
}

.connection-badge.connected {
  color: #72dfad;
}

.workspace {
  background: #091319;
}

.metric-card, .section-card, .notice-card {
  background: #101f26;
  border: 1px solid rgba(117, 184, 196, 0.15);
  border-radius: 14px;
  padding: 16px;
}

.metric-title, .section-subtitle {
  color: #78949e;
  font-size: 11px;
}

.metric-value {
  color: #69e0d7;
  font-size: 22px;
  font-weight: 700;
}

.metric-value.blue { color: #6fbdf2; }
.metric-value.violet { color: #b89ae9; }
.metric-value.good { color: #72dfad; }
.metric-value.warning { color: #e7bb70; }

.section-title {
  font-size: 16px;
  font-weight: 700;
}

.value-row {
  border-bottom: 1px solid rgba(117, 184, 196, 0.09);
  padding: 8px 0;
}

.value-key {
  color: #87a0a9;
}

.value-data {
  color: #dce9eb;
  font-family: monospace;
}

.diagnostic {
  color: #dfbd81;
}

.log-line {
  color: #9bb0b7;
  font-family: monospace;
  font-size: 10px;
}

.read-only-chip {
  background: #182c38;
  border-radius: 999px;
  color: #8fc9e8;
  font-size: 10px;
  font-weight: 700;
  padding: 6px 10px;
}

dropdown.device-selector, button.refresh-button {
  background: #13272e;
  border: 1px solid rgba(117, 184, 196, 0.20);
  border-radius: 9px;
}
";

#[derive(Debug)]
enum CardWorkerInput {
    Refresh(u64),
    SelectDevice(u64, String),
}

#[derive(Debug)]
enum OscillatordWorkerInput {
    RefreshOscillatord(u64),
}

#[derive(Debug)]
pub enum WorkerOutput {
    Snapshot {
        request: u64,
        snapshot: Box<TimeCardSnapshot>,
    },
    Oscillatord {
        request: u64,
        telemetry: Box<Result<OscillatordTelemetry, String>>,
    },
}

struct CardTelemetryWorker {
    backend: Box<dyn TimeCardBackend>,
}

struct OscillatordTelemetryWorker {
    oscillatord_host: String,
    oscillatord_port: u16,
}

impl CardTelemetryWorker {
    fn emit_snapshot(&mut self, request: u64, sender: &ComponentSender<Self>) {
        let snapshot = self.backend.read_snapshot();
        let _ = sender.output(WorkerOutput::Snapshot {
            request,
            snapshot: Box::new(snapshot),
        });
    }
}

impl OscillatordTelemetryWorker {
    fn poll_oscillatord(&self) -> Result<OscillatordTelemetry, String> {
        OscillatordTelemetry::poll(
            &self.oscillatord_host,
            self.oscillatord_port,
            OSCILLATORD_TIMEOUT,
        )
        .map_err(|error| error.to_string())
    }
}

impl Worker for CardTelemetryWorker {
    type Init = AppConfig;
    type Input = CardWorkerInput;
    type Output = WorkerOutput;

    fn init(config: Self::Init, _sender: ComponentSender<Self>) -> Self {
        let backend: Box<dyn TimeCardBackend> = if config.mock {
            Box::new(MockTimeCardBackend::new())
        } else {
            Box::new(LinuxTimeCardBackend::new(
                config.sysfs_root.clone(),
                config.hwmon_root.clone(),
                config.iio_root.clone(),
                config.leds_root.clone(),
            ))
        };
        Self { backend }
    }

    fn update(&mut self, message: Self::Input, sender: ComponentSender<Self>) {
        match message {
            CardWorkerInput::Refresh(request) => self.emit_snapshot(request, &sender),
            CardWorkerInput::SelectDevice(request, device) => {
                self.backend.set_selected_device(&device);
                self.emit_snapshot(request, &sender);
            }
        }
    }
}

impl Worker for OscillatordTelemetryWorker {
    type Init = AppConfig;
    type Input = OscillatordWorkerInput;
    type Output = WorkerOutput;

    fn init(config: Self::Init, _sender: ComponentSender<Self>) -> Self {
        Self {
            oscillatord_host: config.oscillatord_host,
            oscillatord_port: config.oscillatord_port,
        }
    }

    fn update(&mut self, message: Self::Input, sender: ComponentSender<Self>) {
        match message {
            OscillatordWorkerInput::RefreshOscillatord(request) => {
                let _ = sender.output(WorkerOutput::Oscillatord {
                    request,
                    telemetry: Box::new(self.poll_oscillatord()),
                });
            }
        }
    }
}

#[derive(Debug)]
pub enum AppMsg {
    Navigate(Page),
    Refresh,
    Tick,
    SelectDevice(String),
    RefreshOscillatord,
    Worker(Box<WorkerOutput>),
    ClearLog,
    ExportLog,
}

#[allow(clippy::struct_excessive_bools)]
pub struct App {
    config: AppConfig,
    page: Page,
    snapshot: TimeCardSnapshot,
    oscillatord: Option<OscillatordTelemetry>,
    oscillatord_error: String,
    last_updated: String,
    card_busy: bool,
    pending_card_refresh: bool,
    card_request_generation: u64,
    active_card_request: u64,
    oscillatord_busy: bool,
    pending_oscillatord_refresh: bool,
    oscillatord_request_generation: u64,
    active_oscillatord_request: u64,
    tick_count: u8,
    card_worker: WorkerController<CardTelemetryWorker>,
    oscillatord_worker: WorkerController<OscillatordTelemetryWorker>,
    requested_device: Option<String>,
    refresh_timer: Option<glib::SourceId>,
    session_log: SessionLogStore,
    session_log_status: String,
    observed_snapshot: bool,
    observed_oscillatord: bool,
    previous_connected: bool,
    previous_device: String,
    previous_snapshot_error: String,
    previous_oscillatord_error: String,
    offset_history: Vec<f64>,
    window_history: Vec<f64>,
}

#[derive(Default)]
struct StateListCache {
    keys: Vec<String>,
    value_labels: Vec<gtk::Label>,
    rendered_empty: bool,
}

pub struct AppWidgets {
    header_title: gtk::Label,
    header_subtitle: gtk::Label,
    stack: gtk::Stack,
    nav_buttons: Vec<(Page, gtk::ToggleButton)>,
    connection_badge: gtk::Label,
    backend_label: gtk::Label,
    updated_label: gtk::Label,
    device_model: gtk::StringList,
    device_selector: gtk::DropDown,
    selection_guard: Rc<Cell<bool>>,
    refresh_button: gtk::Button,
    refresh_spinner: gtk::Spinner,
    values: HashMap<&'static str, gtk::Label>,
    sma_states: gtk::Box,
    generator_states: gtk::Box,
    counter_states: gtk::Box,
    fpga_states: gtk::Box,
    sensor_states: gtk::Box,
    led_states: gtk::Box,
    capabilities: gtk::Box,
    log_lines: gtk::Box,
    sma_state_cache: StateListCache,
    generator_state_cache: StateListCache,
    counter_state_cache: StateListCache,
    fpga_state_cache: StateListCache,
    sensor_state_cache: StateListCache,
    led_state_cache: StateListCache,
    rendered_capabilities: Option<Vec<String>>,
    rendered_log_lines: Option<Vec<String>>,
    log_status: gtk::Label,
    diagnostic_card: gtk::Box,
    diagnostic_text: gtk::Label,
    oscillatord_progress: gtk::ProgressBar,
    oscillatord_notice: gtk::Box,
    oscillatord_notice_text: gtk::Label,
    offset_chart: gtk::DrawingArea,
    offset_chart_summary: gtk::Label,
    offset_plot_data: Rc<RefCell<Vec<f64>>>,
    window_chart: gtk::DrawingArea,
    window_chart_summary: gtk::Label,
    window_plot_data: Rc<RefCell<Vec<f64>>>,
}

impl App {
    fn next_card_request(&mut self) -> u64 {
        self.card_request_generation = self.card_request_generation.wrapping_add(1);
        self.active_card_request = self.card_request_generation;
        self.card_busy = true;
        self.active_card_request
    }

    fn next_oscillatord_request(&mut self) -> u64 {
        self.oscillatord_request_generation = self.oscillatord_request_generation.wrapping_add(1);
        self.active_oscillatord_request = self.oscillatord_request_generation;
        self.oscillatord_busy = true;
        self.active_oscillatord_request
    }

    fn card_context(&self) -> &str {
        if self.snapshot.device_id.is_empty() {
            "offline"
        } else {
            &self.snapshot.device_id
        }
    }

    fn log(&mut self, severity: SessionLogSeverity, category: &str, message: &str) {
        let context = self.card_context().to_owned();
        self.log_with_context(severity, category, &context, message);
    }

    fn log_with_context(
        &mut self,
        severity: SessionLogSeverity,
        category: &str,
        context: &str,
        message: &str,
    ) {
        self.session_log
            .append(severity, category, context, message);
    }

    #[allow(clippy::cast_precision_loss)]
    fn apply_snapshot(&mut self, snapshot: TimeCardSnapshot) {
        let connected = snapshot.connected;
        let device = snapshot.device_id.clone();
        let context = if device.is_empty() {
            "offline"
        } else {
            &device
        };
        let snapshot_error = snapshot.error_text();
        let device_changed = self.observed_snapshot && device != self.previous_device;

        if !connected || device_changed {
            self.offset_history.clear();
            self.window_history.clear();
        }
        if snapshot.offset_nanoseconds.is_none() {
            self.offset_history.clear();
        } else if let Some(offset) = snapshot.offset_nanoseconds {
            append_bounded(
                &mut self.offset_history,
                offset as f64,
                OFFSET_HISTORY_CAPACITY,
            );
        }
        if snapshot.sample_window_nanoseconds.is_none() {
            self.window_history.clear();
        } else if let Some(window) = snapshot.sample_window_nanoseconds {
            append_bounded(
                &mut self.window_history,
                window as f64,
                WINDOW_HISTORY_CAPACITY,
            );
        }

        if !self.observed_snapshot {
            let (severity, message) = if connected {
                (
                    SessionLogSeverity::Info,
                    format!(
                        "Connected to {} ({})",
                        available(&device),
                        available(&snapshot.board_profile)
                    ),
                )
            } else {
                (SessionLogSeverity::Warn, "No Time Card detected".to_owned())
            };
            self.log_with_context(severity, "Connection", context, &message);
        } else if connected != self.previous_connected {
            let (severity, message) = if connected {
                (
                    SessionLogSeverity::Info,
                    format!("Time Card {} connected", available(&device)),
                )
            } else {
                (
                    SessionLogSeverity::Warn,
                    "Time Card disconnected".to_owned(),
                )
            };
            self.log_with_context(severity, "Connection", context, &message);
        } else if connected && device != self.previous_device {
            self.log_with_context(
                SessionLogSeverity::Info,
                "Connection",
                context,
                &format!("Active Time Card changed to {}", available(&device)),
            );
        }

        if !snapshot_error.is_empty() && snapshot_error != self.previous_snapshot_error {
            self.log_with_context(
                SessionLogSeverity::Warn,
                "Telemetry",
                context,
                &snapshot_error,
            );
        }

        self.observed_snapshot = true;
        self.previous_connected = connected;
        self.previous_device.clone_from(&device);
        self.previous_snapshot_error.clone_from(&snapshot_error);
        self.snapshot = snapshot;
        self.last_updated = Local::now().format("%H:%M:%S").to_string();
    }

    fn apply_oscillatord(&mut self, telemetry: Result<OscillatordTelemetry, String>) {
        let context = format!(
            "oscillatord@{}",
            display_endpoint(&self.config.oscillatord_host, self.config.oscillatord_port)
        );
        match telemetry {
            Ok(telemetry) => {
                if !self.observed_oscillatord || !self.previous_oscillatord_error.is_empty() {
                    self.log_with_context(
                        SessionLogSeverity::Info,
                        "Oscillator",
                        &context,
                        &format!(
                            "oscillatord {} monitoring connected",
                            available(&telemetry.service_version)
                        ),
                    );
                }
                self.oscillatord = Some(telemetry);
                self.oscillatord_error.clear();
                self.previous_oscillatord_error.clear();
            }
            Err(error) => {
                if !self.observed_oscillatord || error != self.previous_oscillatord_error {
                    self.log_with_context(
                        SessionLogSeverity::Warn,
                        "Oscillator",
                        &context,
                        &format!("oscillatord monitoring unavailable: {error}"),
                    );
                }
                self.oscillatord = None;
                self.oscillatord_error.clone_from(&error);
                self.previous_oscillatord_error = error;
            }
        }
        self.observed_oscillatord = true;
    }

    fn finish_card_request(&mut self, request: u64) {
        if request != self.active_card_request {
            return;
        }
        self.card_busy = false;
        if self.pending_card_refresh {
            self.pending_card_refresh = false;
            let request = self.next_card_request();
            self.card_worker.emit(CardWorkerInput::Refresh(request));
        }
    }

    fn finish_oscillatord_request(&mut self, request: u64) {
        if request != self.active_oscillatord_request {
            return;
        }
        self.oscillatord_busy = false;
        if self.pending_oscillatord_refresh {
            self.pending_oscillatord_refresh = false;
            let request = self.next_oscillatord_request();
            self.oscillatord_worker
                .emit(OscillatordWorkerInput::RefreshOscillatord(request));
        }
    }

    fn export_log(&mut self) {
        let directory = UserDirs::new().map_or_else(
            || PathBuf::from("."),
            |directories| {
                directories
                    .document_dir()
                    .unwrap_or_else(|| directories.home_dir())
                    .to_path_buf()
            },
        );
        let stem = format!(
            "timecard-session-{}",
            Utc::now().format("%Y%m%d-%H%M%S-%3f")
        );
        let mut path = directory.join(format!("{stem}.json"));
        let mut suffix = 1_u32;
        while path.exists() {
            path = directory.join(format!("{stem}-{suffix}.json"));
            suffix += 1;
        }

        match self.session_log.write_json(&path) {
            Ok(()) => {
                self.session_log_status = format!("Exported {}", path.display());
                self.log(
                    SessionLogSeverity::Info,
                    "Diagnostics",
                    &format!("Session log exported to {}", path.display()),
                );
            }
            Err(error) => {
                self.session_log_status = format!("Export failed: {error}");
                let message = self.session_log_status.clone();
                self.log(SessionLogSeverity::Error, "Diagnostics", &message);
            }
        }
    }
}

impl SimpleComponent for App {
    type Init = AppConfig;
    type Input = AppMsg;
    type Output = ();
    type Root = adw::ApplicationWindow;
    type Widgets = AppWidgets;

    fn init_root() -> Self::Root {
        adw::ApplicationWindow::builder()
            .title(APP_TITLE)
            .default_width(1360)
            .default_height(880)
            .width_request(960)
            .height_request(640)
            .build()
    }

    fn init(
        config: Self::Init,
        root: Self::Root,
        sender: ComponentSender<Self>,
    ) -> ComponentParts<Self> {
        root.add_css_class("timecard-window");
        let widgets = build_ui(&root, &sender);

        let card_worker = CardTelemetryWorker::builder()
            .detach_worker(config.clone())
            .forward(sender.input_sender(), |output| {
                AppMsg::Worker(Box::new(output))
            });
        let oscillatord_worker = OscillatordTelemetryWorker::builder()
            .detach_worker(config.clone())
            .forward(sender.input_sender(), |output| {
                AppMsg::Worker(Box::new(output))
            });

        let mut session_log = SessionLogStore::new(SESSION_LOG_CAPACITY);
        session_log.append(
            SessionLogSeverity::Info,
            "Application",
            "offline",
            "Control Center session started in read-only mode",
        );

        let mut model = Self {
            page: config.page,
            snapshot: TimeCardSnapshot::default(),
            oscillatord: None,
            oscillatord_error: "Waiting for the monitoring endpoint".to_owned(),
            last_updated: String::new(),
            card_busy: false,
            pending_card_refresh: false,
            card_request_generation: 0,
            active_card_request: 0,
            oscillatord_busy: false,
            pending_oscillatord_refresh: false,
            oscillatord_request_generation: 0,
            active_oscillatord_request: 0,
            tick_count: 0,
            card_worker,
            oscillatord_worker,
            requested_device: None,
            refresh_timer: None,
            session_log,
            session_log_status: "Session log is retained in memory".to_owned(),
            observed_snapshot: false,
            observed_oscillatord: false,
            previous_connected: false,
            previous_device: String::new(),
            previous_snapshot_error: String::new(),
            previous_oscillatord_error: String::new(),
            offset_history: Vec::with_capacity(OFFSET_HISTORY_CAPACITY),
            window_history: Vec::with_capacity(WINDOW_HISTORY_CAPACITY),
            config,
        };

        let request = model.next_card_request();
        model.card_worker.emit(CardWorkerInput::Refresh(request));
        let request = model.next_oscillatord_request();
        model
            .oscillatord_worker
            .emit(OscillatordWorkerInput::RefreshOscillatord(request));

        let timer_sender = sender.input_sender().clone();
        model.refresh_timer = Some(glib::timeout_add_local(POLL_INTERVAL, move || {
            let _ = timer_sender.send(AppMsg::Tick);
            glib::ControlFlow::Continue
        }));

        if let Some(milliseconds) = model.config.quit_after {
            glib::timeout_add_local_once(Duration::from_millis(milliseconds), || {
                relm4::main_application().quit();
            });
        }

        widgets
            .stack
            .set_visible_child_name(model.page.stack_name());
        let (title, subtitle) = page_heading(model.page, &model.snapshot, &model.config);
        widgets.header_title.set_label(title);
        widgets.header_subtitle.set_label(&subtitle);
        for (page, button) in &widgets.nav_buttons {
            set_nav_selected(button, *page == model.page);
        }
        set_metric_tone(&widgets, "overview.gnss", "warning");
        set_metric_tone(&widgets, "overview.window", "warning");
        set_metric_tone(&widgets, "gnss.state", "warning");
        set_metric_tone(&widgets, "osc.service", "warning");

        ComponentParts { model, widgets }
    }

    fn update(&mut self, message: Self::Input, _sender: ComponentSender<Self>) {
        match message {
            AppMsg::Navigate(page) => self.page = page,
            AppMsg::Tick => {
                self.tick_count = (self.tick_count + 1) % OSCILLATORD_POLL_TICKS;
                if self.card_busy {
                    self.pending_card_refresh = true;
                } else {
                    let request = self.next_card_request();
                    self.card_worker.emit(CardWorkerInput::Refresh(request));
                }
                if self.tick_count == 0 {
                    if self.oscillatord_busy {
                        self.pending_oscillatord_refresh = true;
                    } else {
                        let request = self.next_oscillatord_request();
                        self.oscillatord_worker
                            .emit(OscillatordWorkerInput::RefreshOscillatord(request));
                    }
                }
            }
            AppMsg::Refresh => {
                if self.card_busy {
                    self.pending_card_refresh = true;
                } else {
                    let request = self.next_card_request();
                    self.card_worker.emit(CardWorkerInput::Refresh(request));
                }
            }
            AppMsg::SelectDevice(device) => {
                if device.is_empty()
                    || self
                        .requested_device
                        .as_deref()
                        .unwrap_or(&self.snapshot.device_id)
                        == device
                {
                    return;
                }
                self.log_with_context(
                    SessionLogSeverity::Info,
                    "Connection",
                    &device,
                    &format!("Switching to Time Card {device}"),
                );
                self.offset_history.clear();
                self.window_history.clear();
                self.requested_device = Some(device.clone());
                self.snapshot = TimeCardSnapshot {
                    connected: true,
                    backend_name: self.snapshot.backend_name.clone(),
                    device_id: device.clone(),
                    available_devices: self.snapshot.available_devices.clone(),
                    ..TimeCardSnapshot::default()
                };
                self.last_updated.clear();
                let request = self.next_card_request();
                self.card_worker
                    .emit(CardWorkerInput::SelectDevice(request, device));
            }
            AppMsg::RefreshOscillatord => {
                if self.oscillatord_busy {
                    self.pending_oscillatord_refresh = true;
                } else {
                    let request = self.next_oscillatord_request();
                    self.oscillatord_worker
                        .emit(OscillatordWorkerInput::RefreshOscillatord(request));
                }
            }
            AppMsg::Worker(output) => match *output {
                WorkerOutput::Snapshot { request, snapshot } => {
                    if request == self.active_card_request {
                        self.apply_snapshot(*snapshot);
                        self.requested_device = None;
                    }
                    self.finish_card_request(request);
                }
                WorkerOutput::Oscillatord { request, telemetry } => {
                    if request == self.active_oscillatord_request {
                        self.apply_oscillatord(*telemetry);
                    }
                    self.finish_oscillatord_request(request);
                }
            },
            AppMsg::ClearLog => {
                self.session_log.clear();
                "Session log cleared".clone_into(&mut self.session_log_status);
                self.log(
                    SessionLogSeverity::Info,
                    "Diagnostics",
                    "Session log cleared",
                );
            }
            AppMsg::ExportLog => self.export_log(),
        }
    }

    #[allow(clippy::too_many_lines)]
    fn update_view(&self, widgets: &mut Self::Widgets, _sender: ComponentSender<Self>) {
        widgets.stack.set_visible_child_name(self.page.stack_name());
        let (title, subtitle) = page_heading(self.page, &self.snapshot, &self.config);
        widgets.header_title.set_label(title);
        widgets.header_subtitle.set_label(&subtitle);

        for (page, button) in &widgets.nav_buttons {
            set_nav_selected(button, *page == self.page);
        }

        let connection_state = if self.requested_device.is_some() {
            "LOADING TIME CARD"
        } else if !self.snapshot.connected {
            if self.card_busy && !self.observed_snapshot {
                "STARTING TELEMETRY"
            } else {
                "WAITING FOR TIME CARD"
            }
        } else if self.snapshot.offset_nanoseconds.is_some() {
            "TIME CARD READY"
        } else if self.snapshot.phc_tai_nanoseconds.is_some() {
            "TIME CARD TIMING LIMITED"
        } else {
            "TIME CARD DISCOVERED"
        };
        widgets.connection_badge.set_label(connection_state);
        if self.snapshot.offset_nanoseconds.is_some() {
            widgets.connection_badge.add_css_class("connected");
        } else {
            widgets.connection_badge.remove_css_class("connected");
        }
        widgets
            .backend_label
            .set_label(&available(&self.snapshot.backend_name));
        let updated_text = if self.last_updated.is_empty() {
            "Waiting for first sample".to_owned()
        } else if self.snapshot.phc_tai_nanoseconds.is_some() {
            format!("Sampled {}", self.last_updated)
        } else {
            format!("Polled {}; no timing sample", self.last_updated)
        };
        widgets.updated_label.set_label(&updated_text);

        widgets.refresh_button.set_sensitive(!self.card_busy);
        let any_busy = self.card_busy || self.oscillatord_busy;
        widgets.refresh_spinner.set_spinning(any_busy);
        widgets.refresh_spinner.set_visible(any_busy);
        sync_device_selector(self, widgets);
        update_snapshot_values(self, widgets);
        update_oscillatord_values(self, widgets);
        widgets
            .offset_plot_data
            .borrow_mut()
            .clone_from(&self.offset_history);
        widgets
            .window_plot_data
            .borrow_mut()
            .clone_from(&self.window_history);
        let offset_summary = history_summary(&self.offset_history, "nanoseconds");
        let window_summary = history_summary(&self.window_history, "nanoseconds");
        widgets.offset_chart_summary.set_label(&offset_summary);
        widgets.window_chart_summary.set_label(&window_summary);
        widgets.offset_chart.set_tooltip_text(Some(&offset_summary));
        widgets.window_chart.set_tooltip_text(Some(&window_summary));
        widgets.offset_chart.queue_draw();
        widgets.window_chart.queue_draw();

        update_state_list_if_changed(
            &widgets.sma_states,
            &mut widgets.sma_state_cache,
            &self.snapshot.sma_states,
            "No readable SMA routing attributes",
        );
        update_state_list_if_changed(
            &widgets.generator_states,
            &mut widgets.generator_state_cache,
            &self.snapshot.generator_states,
            "No readable generator attributes",
        );
        update_state_list_if_changed(
            &widgets.counter_states,
            &mut widgets.counter_state_cache,
            &self.snapshot.frequency_counter_states,
            "No readable counter attributes",
        );
        update_state_list_if_changed(
            &widgets.fpga_states,
            &mut widgets.fpga_state_cache,
            &self.snapshot.fpga_engine_states,
            "No extended FPGA engine status attributes are readable",
        );
        update_state_list_if_changed(
            &widgets.sensor_states,
            &mut widgets.sensor_state_cache,
            &self.snapshot.sensor_states,
            "No supported sensor routes are readable for this Time Card",
        );
        update_state_list_if_changed(
            &widgets.led_states,
            &mut widgets.led_state_cache,
            &self.snapshot.led_states,
            "No Time Card LED classes are readable",
        );
        update_capabilities_if_changed(
            &widgets.capabilities,
            &mut widgets.rendered_capabilities,
            &self.snapshot.capabilities,
        );
        let log_lines = self.session_log.text_lines(Some(9));
        update_log_if_changed(
            &widgets.log_lines,
            &mut widgets.rendered_log_lines,
            &log_lines,
        );
        widgets.log_status.set_label(&format!(
            "{}; {} retained, {} dropped",
            self.session_log_status,
            self.session_log.len(),
            self.session_log.dropped_record_count()
        ));

        let diagnostic = self.snapshot.error_text();
        widgets.diagnostic_card.set_visible(!diagnostic.is_empty());
        widgets.diagnostic_text.set_label(&diagnostic);
        widgets
            .oscillatord_notice
            .set_visible(self.oscillatord.is_none());
        widgets.oscillatord_notice_text.set_label(&format!(
            "Start oscillatord with monitoring enabled at {}, or pass --oscillatord-host and --oscillatord-port. This client never sends control requests.\n\n{}",
            display_endpoint(
                &self.config.oscillatord_host,
                self.config.oscillatord_port
            ),
            available(&self.oscillatord_error)
        ));
    }

    fn shutdown(&mut self, _widgets: &mut Self::Widgets, _output: relm4::Sender<Self::Output>) {
        if let Some(timer) = self.refresh_timer.take() {
            timer.remove();
        }
    }
}

#[allow(clippy::too_many_lines)]
fn build_ui(root: &adw::ApplicationWindow, sender: &ComponentSender<App>) -> AppWidgets {
    let shell = gtk::Box::new(gtk::Orientation::Vertical, 0);
    root.set_content(Some(&shell));

    let header = adw::HeaderBar::new();
    header.add_css_class("timecard-header");
    shell.append(&header);

    let heading = gtk::Box::new(gtk::Orientation::Vertical, 1);
    let header_title = left_label("Precision timing overview", "header-title");
    let header_subtitle = left_label("Discovering Time Card hardware", "header-subtitle");
    heading.append(&header_title);
    heading.append(&header_subtitle);
    header.set_title_widget(Some(&heading));

    let read_only = gtk::Label::new(Some("READ ONLY"));
    read_only.add_css_class("read-only-chip");
    header.pack_start(&read_only);

    let selection_guard = Rc::new(Cell::new(false));
    let device_model = gtk::StringList::new(&[]);
    let device_selector = gtk::DropDown::new(Some(device_model.clone()), None::<gtk::Expression>);
    device_selector.add_css_class("device-selector");
    device_selector.set_tooltip_text(Some("Select a discovered Time Card"));
    device_selector.set_sensitive(false);
    {
        let sender = sender.input_sender().clone();
        let model = device_model.clone();
        let guard = Rc::clone(&selection_guard);
        device_selector.connect_selected_notify(move |selector| {
            if guard.get() {
                return;
            }
            if let Some(device) = model.string(selector.selected()) {
                let _ = sender.send(AppMsg::SelectDevice(device.to_string()));
            }
        });
    }
    header.pack_end(&device_selector);

    let refresh_spinner = gtk::Spinner::new();
    refresh_spinner.set_visible(false);
    header.pack_end(&refresh_spinner);

    let refresh_button = gtk::Button::builder()
        .icon_name("view-refresh-symbolic")
        .tooltip_text("Refresh telemetry now")
        .build();
    refresh_button.add_css_class("refresh-button");
    {
        let sender = sender.input_sender().clone();
        refresh_button.connect_clicked(move |_| {
            let _ = sender.send(AppMsg::Refresh);
        });
    }
    header.pack_end(&refresh_button);

    let body = gtk::Paned::new(gtk::Orientation::Horizontal);
    body.set_vexpand(true);
    body.set_position(246);
    body.set_resize_start_child(false);
    body.set_shrink_start_child(false);
    shell.append(&body);

    let sidebar = gtk::Box::new(gtk::Orientation::Vertical, 8);
    sidebar.add_css_class("sidebar");
    sidebar.set_width_request(246);
    body.set_start_child(Some(&sidebar));

    let brand = gtk::Box::new(gtk::Orientation::Horizontal, 11);
    brand.set_margin_bottom(22);
    let brand_mark = gtk::Label::new(Some("TC"));
    brand_mark.add_css_class("brand-mark");
    brand.append(&brand_mark);
    let brand_text = gtk::Box::new(gtk::Orientation::Vertical, 0);
    brand_text.append(&left_label("TIME CARD", "brand-title"));
    brand_text.append(&left_label("CONTROL CENTER", "eyebrow"));
    brand.append(&brand_text);
    sidebar.append(&brand);
    sidebar.append(&left_label("WORKSPACES", "eyebrow"));

    let mut nav_buttons = Vec::new();
    for (page, title, icon) in [
        (Page::Overview, "Overview", "view-grid-symbolic"),
        (
            Page::TimingIo,
            "Timing I/O",
            "network-transmit-receive-symbolic",
        ),
        (Page::Sensors, "Sensors and LEDs", "weather-clear-symbolic"),
        (Page::Gnss, "GNSS and serial", "find-location-symbolic"),
        (
            Page::Oscillatord,
            "oscillatord",
            "preferences-system-symbolic",
        ),
    ] {
        let button = nav_button(title, icon);
        let input = sender.input_sender().clone();
        button.connect_clicked(move |_| {
            let _ = input.send(AppMsg::Navigate(page));
        });
        sidebar.append(&button);
        nav_buttons.push((page, button));
    }

    let sidebar_spacer = gtk::Box::new(gtk::Orientation::Vertical, 0);
    sidebar_spacer.set_vexpand(true);
    sidebar.append(&sidebar_spacer);

    let connection_card = gtk::Box::new(gtk::Orientation::Vertical, 4);
    connection_card.add_css_class("connection-card");
    let connection_badge = left_label("STARTING TELEMETRY", "connection-badge");
    let backend_label = left_label("Initializing backend", "muted");
    backend_label.set_wrap(true);
    let updated_label = left_label("Waiting for first sample", "eyebrow");
    connection_card.append(&connection_badge);
    connection_card.append(&backend_label);
    connection_card.append(&updated_label);
    sidebar.append(&connection_card);

    let stack = gtk::Stack::new();
    stack.add_css_class("workspace");
    stack.set_transition_type(gtk::StackTransitionType::Crossfade);
    stack.set_transition_duration(180);
    stack.set_hexpand(true);
    stack.set_vexpand(true);
    body.set_end_child(Some(&stack));

    let mut values = HashMap::new();
    let overview = build_overview_page(sender, &mut values);
    let timing = build_timing_page(&mut values);
    let sensors = build_sensors_page(&mut values);
    let gnss = build_gnss_page(&mut values);
    let oscillator = build_oscillatord_page(sender, &mut values);

    stack.add_named(&overview.page, Some(Page::Overview.stack_name()));
    stack.add_named(&timing.page, Some(Page::TimingIo.stack_name()));
    stack.add_named(&sensors.page, Some(Page::Sensors.stack_name()));
    stack.add_named(&gnss.page, Some(Page::Gnss.stack_name()));
    stack.add_named(&oscillator.page, Some(Page::Oscillatord.stack_name()));

    AppWidgets {
        header_title,
        header_subtitle,
        stack,
        nav_buttons,
        connection_badge,
        backend_label,
        updated_label,
        device_model,
        device_selector,
        selection_guard,
        refresh_button,
        refresh_spinner,
        values,
        sma_states: timing.sma_states,
        generator_states: timing.generator_states,
        counter_states: timing.counter_states,
        fpga_states: timing.fpga_states,
        sensor_states: sensors.sensor_states,
        led_states: sensors.led_states,
        capabilities: gnss.capabilities,
        log_lines: overview.log_lines,
        sma_state_cache: StateListCache::default(),
        generator_state_cache: StateListCache::default(),
        counter_state_cache: StateListCache::default(),
        fpga_state_cache: StateListCache::default(),
        sensor_state_cache: StateListCache::default(),
        led_state_cache: StateListCache::default(),
        rendered_capabilities: None,
        rendered_log_lines: None,
        log_status: overview.log_status,
        diagnostic_card: overview.diagnostic_card,
        diagnostic_text: overview.diagnostic_text,
        oscillatord_progress: oscillator.progress,
        oscillatord_notice: oscillator.notice,
        oscillatord_notice_text: oscillator.notice_text,
        offset_chart: overview.offset_chart,
        offset_chart_summary: overview.offset_chart_summary,
        offset_plot_data: overview.offset_plot_data,
        window_chart: overview.window_chart,
        window_chart_summary: overview.window_chart_summary,
        window_plot_data: overview.window_plot_data,
    }
}

struct OverviewPage {
    page: gtk::ScrolledWindow,
    log_lines: gtk::Box,
    log_status: gtk::Label,
    diagnostic_card: gtk::Box,
    diagnostic_text: gtk::Label,
    offset_chart: gtk::DrawingArea,
    offset_chart_summary: gtk::Label,
    offset_plot_data: Rc<RefCell<Vec<f64>>>,
    window_chart: gtk::DrawingArea,
    window_chart_summary: gtk::Label,
    window_plot_data: Rc<RefCell<Vec<f64>>>,
}

#[allow(clippy::too_many_lines)]
fn build_overview_page(
    sender: &ComponentSender<App>,
    values: &mut HashMap<&'static str, gtk::Label>,
) -> OverviewPage {
    let (page, content) = page_scaffold();
    content.append(&metric_grid(&[
        metric_card(
            values,
            "overview.offset",
            "PHC to system",
            "TAI-aware comparison",
            "",
        ),
        metric_card(
            values,
            "overview.gnss",
            "PPS supervisor",
            "Current FPGA state",
            "good",
        ),
        metric_card(
            values,
            "overview.window",
            "Sample window",
            "Bracketed PHC read",
            "blue",
        ),
        metric_card(
            values,
            "overview.tai",
            "UTC to TAI",
            "Offset source",
            "violet",
        ),
    ]));

    let precision = section_card(
        "Precision clock",
        "UTC presentation with TAI-aware comparison",
    );
    add_value_row(&precision, values, "overview.phc", "Time Card PHC");
    add_value_row(&precision, values, "overview.system", "Linux system");
    add_value_row(&precision, values, "overview.clock_source", "Clock source");
    add_value_row(&precision, values, "overview.fpga_offset", "FPGA offset");
    add_value_row(&precision, values, "overview.drift", "FPGA drift");
    add_value_row(&precision, values, "overview.method", "Timestamp method");

    let hardware = section_card("Hardware identity", "ptp_ocp discovery and endpoints");
    add_value_row(&hardware, values, "identity.device", "Device");
    add_value_row(&hardware, values, "identity.profile", "Board profile");
    add_value_row(&hardware, values, "identity.pci", "PCI identity");
    add_value_row(&hardware, values, "identity.serial", "Serial number");
    add_value_row(&hardware, values, "identity.ptp", "PTP node");
    add_value_row(&hardware, values, "identity.pps", "PPS node");
    add_value_row(&hardware, values, "identity.sysfs", "Sysfs");
    content.append(&two_columns(&precision, &hardware));

    let (offset_card, offset_chart, offset_chart_summary, offset_plot_data) = history_chart(
        "PHC offset history",
        "Last 200 one-second samples, nanoseconds",
        (0.19, 0.84, 0.82),
    );
    content.append(&offset_card);
    let (window_card, window_chart, window_chart_summary, window_plot_data) = history_chart(
        "Sampling window history",
        "Last 60 bracketed samples, nanoseconds",
        (0.37, 0.72, 1.0),
    );
    content.append(&window_card);

    let diagnostic_card = section_card("Diagnostic", "Backend and timing observations");
    let diagnostic_text = left_label("", "diagnostic");
    diagnostic_text.set_wrap(true);
    diagnostic_card.append(&diagnostic_text);
    diagnostic_card.set_visible(false);
    content.append(&diagnostic_card);

    let log_card = section_card("Session log", "Bounded diagnostics retained for this run");
    let log_status = left_label("Session log is retained in memory", "section-subtitle");
    log_card.append(&log_status);
    let log_lines = gtk::Box::new(gtk::Orientation::Vertical, 3);
    log_lines.set_margin_top(8);
    log_card.append(&log_lines);
    let actions = gtk::Box::new(gtk::Orientation::Horizontal, 8);
    actions.set_halign(gtk::Align::End);
    actions.set_margin_top(10);
    let clear = gtk::Button::with_label("Clear");
    let export = gtk::Button::with_label("Export JSON");
    export.add_css_class("suggested-action");
    {
        let input = sender.input_sender().clone();
        clear.connect_clicked(move |_| {
            let _ = input.send(AppMsg::ClearLog);
        });
    }
    {
        let input = sender.input_sender().clone();
        export.connect_clicked(move |_| {
            let _ = input.send(AppMsg::ExportLog);
        });
    }
    actions.append(&clear);
    actions.append(&export);
    log_card.append(&actions);
    content.append(&log_card);

    OverviewPage {
        page,
        log_lines,
        log_status,
        diagnostic_card,
        diagnostic_text,
        offset_chart,
        offset_chart_summary,
        offset_plot_data,
        window_chart,
        window_chart_summary,
        window_plot_data,
    }
}

struct TimingPage {
    page: gtk::ScrolledWindow,
    sma_states: gtk::Box,
    generator_states: gtk::Box,
    counter_states: gtk::Box,
    fpga_states: gtk::Box,
}

fn build_timing_page(values: &mut HashMap<&'static str, gtk::Label>) -> TimingPage {
    let (page, content) = page_scaffold();
    content.append(&metric_grid(&[
        metric_card(
            values,
            "timing.sma_count",
            "SMA routes",
            "Current routing",
            "",
        ),
        metric_card(
            values,
            "timing.generator_count",
            "Signal generators",
            "Waveform state",
            "blue",
        ),
        metric_card(
            values,
            "timing.counter_count",
            "Frequency counters",
            "Gate readings",
            "violet",
        ),
        metric_card(
            values,
            "timing.fpga_count",
            "FPGA status",
            "Timing engines",
            "warning",
        ),
    ]));

    let sma_card = section_card("SMA connector routing", "Successful ptp_ocp sysfs reads");
    let sma_states = dynamic_list(&sma_card);
    let counter_card = section_card("Frequency counters", "Gate and latest reading");
    let counter_states = dynamic_list(&counter_card);
    content.append(&two_columns(&sma_card, &counter_card));

    let generator_card = section_card(
        "Signal generators",
        "Waveform, start, repeat, polarity and cable-delay inventory",
    );
    let generator_states = dynamic_list(&generator_card);
    content.append(&generator_card);

    let fpga_card = section_card(
        "FPGA engine status",
        "PPS, NMEA, ToD, IRIG and DCF status where supported",
    );
    let fpga_states = dynamic_list(&fpga_card);
    add_value_row(
        &fpga_card,
        values,
        "timing.image_contract",
        "Image contract",
    );
    content.append(&fpga_card);
    content.append(&notice(
        "Read-only scope",
        "This workspace performs reads only. Root-owned sysfs writes and sticky-fault acknowledgements are never issued by the dashboard.",
    ));

    TimingPage {
        page,
        sma_states,
        generator_states,
        counter_states,
        fpga_states,
    }
}

struct SensorsPage {
    page: gtk::ScrolledWindow,
    sensor_states: gtk::Box,
    led_states: gtk::Box,
}

fn build_sensors_page(values: &mut HashMap<&'static str, gtk::Label>) -> SensorsPage {
    let (page, content) = page_scaffold();
    content.append(&metric_grid(&[
        metric_card(
            values,
            "sensors.profile",
            "Peripheral profile",
            "Selected hardware",
            "",
        ),
        metric_card(
            values,
            "sensors.count",
            "Sensor channels",
            "Linux hwmon and IIO",
            "blue",
        ),
        metric_card(
            values,
            "leds.count",
            "Status LEDs",
            "Selected PCI function",
            "violet",
        ),
    ]));

    let sensor_card = section_card(
        "Environmental sensors",
        "LM75B, SHT3x and ICP-10100 standard interfaces",
    );
    let sensor_states = dynamic_list(&sensor_card);
    let led_card = section_card(
        "Front-panel LEDs",
        "Brightness and RGB intensity, read-only",
    );
    let led_states = dynamic_list(&led_card);
    content.append(&two_columns(&sensor_card, &led_card));
    content.append(&notice(
        "Selected-card telemetry",
        "This dashboard never writes LED state. Sensor and LED reads are scoped to the selected Time Card PCI function. Missing supported telemetry is reported as unavailable, not as a GNSS fault.",
    ));

    SensorsPage {
        page,
        sensor_states,
        led_states,
    }
}

struct GnssPage {
    page: gtk::ScrolledWindow,
    capabilities: gtk::Box,
}

fn build_gnss_page(values: &mut HashMap<&'static str, gtk::Label>) -> GnssPage {
    let (page, content) = page_scaffold();
    content.append(&metric_grid(&[
        metric_card(
            values,
            "gnss.state",
            "PPS supervisor",
            "Synchronization state",
            "good",
        ),
        metric_card(
            values,
            "gnss.protocol",
            "ToD protocol",
            "Serial timing format",
            "blue",
        ),
        metric_card(
            values,
            "gnss.source",
            "Clock source",
            "Current FPGA selection",
            "violet",
        ),
    ]));

    let serial = section_card("Serial endpoints", "Read-only discovery from ptp_ocp sysfs");
    add_value_row(&serial, values, "gnss.primary", "Primary GNSS");
    add_value_row(&serial, values, "gnss.secondary", "Secondary GNSS");
    add_value_row(&serial, values, "gnss.atomic", "Atomic clock");
    add_value_row(&serial, values, "gnss.nmea", "NMEA output");
    add_value_row(&serial, values, "gnss.i2c", "I2C adapter");
    add_value_row(&serial, values, "gnss.mro50", "mRO-50 bridge");

    let capability_card = section_card(
        "Detected capabilities",
        "Workspaces are gated by hardware resources",
    );
    let capabilities = gtk::Box::new(gtk::Orientation::Vertical, 6);
    capability_card.append(&capabilities);
    content.append(&two_columns(&serial, &capability_card));
    content.append(&notice(
        "UART monitoring",
        "The backend discovers Linux UART endpoints without opening them. Passive UBX and NMEA stream decoding can be added without requiring this dashboard to run as root.",
    ));

    GnssPage { page, capabilities }
}

struct OscillatordPage {
    page: gtk::ScrolledWindow,
    progress: gtk::ProgressBar,
    notice: gtk::Box,
    notice_text: gtk::Label,
}

fn build_oscillatord_page(
    sender: &ComponentSender<App>,
    values: &mut HashMap<&'static str, gtk::Label>,
) -> OscillatordPage {
    let (page, content) = page_scaffold();
    content.append(&metric_grid(&[
        metric_card(
            values,
            "osc.service",
            "Monitoring service",
            "Protocol v1 status",
            "good",
        ),
        metric_card(
            values,
            "osc.discipline",
            "Discipline state",
            "Convergence",
            "",
        ),
        metric_card(
            values,
            "osc.gnss",
            "GNSS",
            "Reported by oscillatord",
            "blue",
        ),
    ]));

    let details = section_card(
        "Oscillator discipline",
        "Read-only status request { request: 0 }",
    );
    add_value_row(&details, values, "osc.version", "Service");
    add_value_row(&details, values, "osc.endpoint", "Endpoint");
    add_value_row(&details, values, "osc.action", "Status action");
    add_value_row(&details, values, "osc.status", "State");
    add_value_row(&details, values, "osc.holdover", "Holdover");
    add_value_row(&details, values, "osc.clock", "Clock");
    add_value_row(&details, values, "osc.convergence", "Convergence");
    add_value_row(&details, values, "osc.oscillator", "Oscillator");
    add_value_row(&details, values, "osc.controls", "Controls");
    add_value_row(&details, values, "osc.gnss_detail", "GNSS quality");
    add_value_row(&details, values, "osc.antenna", "Antenna");
    add_value_row(&details, values, "osc.policy", "Control policy");
    let progress = gtk::ProgressBar::new();
    progress.set_margin_top(12);
    progress.set_show_text(true);
    details.append(&progress);
    let refresh = gtk::Button::with_label("Refresh oscillatord");
    refresh.set_halign(gtk::Align::End);
    refresh.set_margin_top(10);
    {
        let input = sender.input_sender().clone();
        refresh.connect_clicked(move |_| {
            let _ = input.send(AppMsg::RefreshOscillatord);
        });
    }
    details.append(&refresh);
    content.append(&details);

    let notice = section_card(
        "Monitoring endpoint",
        "oscillatord is not currently available",
    );
    let notice_text = left_label("Waiting for the monitoring endpoint", "diagnostic");
    notice_text.set_wrap(true);
    notice.append(&notice_text);
    content.append(&notice);

    OscillatordPage {
        page,
        progress,
        notice,
        notice_text,
    }
}

fn page_scaffold() -> (gtk::ScrolledWindow, gtk::Box) {
    let page = gtk::ScrolledWindow::new();
    page.set_policy(gtk::PolicyType::Never, gtk::PolicyType::Automatic);
    page.set_hexpand(true);
    page.set_vexpand(true);
    let content = gtk::Box::new(gtk::Orientation::Vertical, 16);
    content.set_margin_top(22);
    content.set_margin_bottom(28);
    content.set_margin_start(24);
    content.set_margin_end(24);
    page.set_child(Some(&content));
    (page, content)
}

fn left_label(text: &str, class: &str) -> gtk::Label {
    let label = gtk::Label::new(Some(text));
    label.set_halign(gtk::Align::Start);
    label.set_xalign(0.0);
    label.add_css_class(class);
    label
}

fn nav_button(title: &str, icon_name: &str) -> gtk::ToggleButton {
    let button = gtk::ToggleButton::new();
    button.add_css_class("nav-button");
    button.set_hexpand(true);
    button.set_tooltip_text(Some(title));
    let content = gtk::Box::new(gtk::Orientation::Horizontal, 10);
    content.append(&gtk::Image::from_icon_name(icon_name));
    let title = left_label(title, "nav-label");
    title.set_hexpand(true);
    content.append(&title);
    button.set_child(Some(&content));
    button
}

fn set_nav_selected(button: &gtk::ToggleButton, selected: bool) {
    button.set_active(selected);
    if selected {
        button.add_css_class("selected");
    } else {
        button.remove_css_class("selected");
    }
}

fn metric_card(
    values: &mut HashMap<&'static str, gtk::Label>,
    id: &'static str,
    title: &str,
    detail: &str,
    tone: &str,
) -> gtk::Box {
    let card = gtk::Box::new(gtk::Orientation::Vertical, 6);
    card.add_css_class("metric-card");
    card.set_width_request(205);
    card.set_hexpand(true);
    card.append(&left_label(title, "metric-title"));
    let value = left_label("Unavailable", "metric-value");
    if !tone.is_empty() {
        value.add_css_class(tone);
    }
    value.set_ellipsize(gtk::pango::EllipsizeMode::End);
    card.append(&value);
    let detail = left_label(detail, "metric-title");
    detail.set_wrap(true);
    card.append(&detail);
    values.insert(id, value);
    card
}

fn metric_grid(cards: &[gtk::Box]) -> gtk::FlowBox {
    let grid = gtk::FlowBox::new();
    grid.set_selection_mode(gtk::SelectionMode::None);
    grid.set_homogeneous(true);
    grid.set_min_children_per_line(2);
    grid.set_max_children_per_line(4);
    grid.set_column_spacing(12);
    grid.set_row_spacing(12);
    for card in cards {
        grid.insert(card, -1);
    }
    grid
}

fn section_card(title: &str, subtitle: &str) -> gtk::Box {
    let card = gtk::Box::new(gtk::Orientation::Vertical, 2);
    card.add_css_class("section-card");
    card.set_hexpand(true);
    card.append(&left_label(title, "section-title"));
    let subtitle = left_label(subtitle, "section-subtitle");
    subtitle.set_margin_bottom(8);
    subtitle.set_wrap(true);
    card.append(&subtitle);
    card
}

fn history_chart(
    title: &str,
    subtitle: &str,
    color: (f64, f64, f64),
) -> (
    gtk::Box,
    gtk::DrawingArea,
    gtk::Label,
    Rc<RefCell<Vec<f64>>>,
) {
    let card = section_card(title, subtitle);
    let chart = gtk::DrawingArea::new();
    chart.set_content_height(170);
    chart.set_hexpand(true);
    chart.set_margin_top(8);
    chart.set_tooltip_text(Some(subtitle));
    let plot_data = Rc::new(RefCell::new(Vec::new()));
    let draw_data = Rc::clone(&plot_data);
    chart.set_draw_func(move |_area, context, width, height| {
        draw_history(context, width, height, &draw_data.borrow(), color);
    });
    card.append(&chart);
    let summary = left_label("No timing samples", "section-subtitle");
    summary.set_selectable(true);
    summary.set_margin_top(4);
    card.append(&summary);
    (card, chart, summary, plot_data)
}

#[allow(clippy::cast_precision_loss)]
fn draw_history(
    context: &gtk::cairo::Context,
    width: i32,
    height: i32,
    samples: &[f64],
    color: (f64, f64, f64),
) {
    const LEFT: f64 = 12.0;
    const RIGHT: f64 = 12.0;
    const TOP: f64 = 12.0;
    const BOTTOM: f64 = 16.0;

    let width = f64::from(width);
    let height = f64::from(height);
    if width <= 0.0 || height <= 0.0 {
        return;
    }

    let plot_width = (width - LEFT - RIGHT).max(1.0);
    let plot_height = (height - TOP - BOTTOM).max(1.0);

    context.set_line_width(1.0);
    context.set_source_rgba(0.48, 0.65, 0.69, 0.14);
    for line in 0..=4 {
        let y = TOP + plot_height * f64::from(line) / 4.0;
        context.move_to(LEFT, y);
        context.line_to(LEFT + plot_width, y);
    }
    let _ = context.stroke();

    if samples.is_empty() {
        context.set_source_rgba(0.58, 0.68, 0.71, 0.72);
        context.set_font_size(12.0);
        context.move_to(LEFT + 8.0, TOP + plot_height / 2.0);
        let _ = context.show_text("Waiting for timing samples");
        return;
    }

    let extent = samples
        .iter()
        .fold(1.0_f64, |extent, sample| extent.max(sample.abs()))
        * 1.1;
    let minimum = -extent;
    let maximum = extent;
    let span = maximum - minimum;

    let zero_y = TOP + (maximum / span) * plot_height;
    context.set_source_rgba(0.72, 0.79, 0.81, 0.28);
    context.move_to(LEFT, zero_y);
    context.line_to(LEFT + plot_width, zero_y);
    let _ = context.stroke();

    context.set_source_rgba(color.0, color.1, color.2, 0.16);
    context.move_to(LEFT, zero_y);
    for (index, sample) in samples.iter().enumerate() {
        let x = if samples.len() == 1 {
            LEFT + plot_width / 2.0
        } else {
            LEFT + plot_width * index as f64 / (samples.len() - 1) as f64
        };
        let y = TOP + (maximum - sample) / span * plot_height;
        context.line_to(x, y);
    }
    context.line_to(LEFT + plot_width, zero_y);
    context.close_path();
    let _ = context.fill();

    context.set_source_rgb(color.0, color.1, color.2);
    context.set_line_width(2.0);
    context.set_line_cap(gtk::cairo::LineCap::Round);
    for (index, sample) in samples.iter().enumerate() {
        let x = if samples.len() == 1 {
            LEFT + plot_width / 2.0
        } else {
            LEFT + plot_width * index as f64 / (samples.len() - 1) as f64
        };
        let y = TOP + (maximum - sample) / span * plot_height;
        if index == 0 {
            context.move_to(x, y);
        } else {
            context.line_to(x, y);
        }
    }
    let _ = context.stroke();
}

fn add_value_row(
    parent: &gtk::Box,
    values: &mut HashMap<&'static str, gtk::Label>,
    id: &'static str,
    title: &str,
) {
    let row = gtk::Box::new(gtk::Orientation::Horizontal, 18);
    row.add_css_class("value-row");
    let key = left_label(title, "value-key");
    key.set_hexpand(true);
    let value = gtk::Label::new(Some("Unavailable"));
    value.set_halign(gtk::Align::End);
    value.set_xalign(1.0);
    value.set_selectable(true);
    value.set_wrap(true);
    value.set_max_width_chars(52);
    value.add_css_class("value-data");
    row.append(&key);
    row.append(&value);
    parent.append(&row);
    values.insert(id, value);
}

fn dynamic_list(parent: &gtk::Box) -> gtk::Box {
    let list = gtk::Box::new(gtk::Orientation::Vertical, 0);
    parent.append(&list);
    list
}

fn two_columns(left: &gtk::Box, right: &gtk::Box) -> gtk::Box {
    let columns = gtk::Box::new(gtk::Orientation::Horizontal, 16);
    left.set_hexpand(true);
    right.set_hexpand(true);
    left.set_homogeneous(false);
    right.set_homogeneous(false);
    columns.append(left);
    columns.append(right);
    columns
}

fn notice(title: &str, text: &str) -> gtk::Box {
    let card = gtk::Box::new(gtk::Orientation::Vertical, 6);
    card.add_css_class("notice-card");
    card.append(&left_label(title, "section-title"));
    let text = left_label(text, "muted");
    text.set_wrap(true);
    card.append(&text);
    card
}

fn sync_device_selector(model: &App, widgets: &AppWidgets) {
    let target_device = model
        .requested_device
        .as_deref()
        .unwrap_or(&model.snapshot.device_id);
    let devices =
        if model.snapshot.available_devices.is_empty() && !model.snapshot.device_id.is_empty() {
            vec![model.snapshot.device_id.clone()]
        } else {
            model.snapshot.available_devices.clone()
        };
    let current: Vec<String> = (0..widgets.device_model.n_items())
        .filter_map(|position| widgets.device_model.string(position))
        .map(|value| value.to_string())
        .collect();
    let target = u32::try_from(
        devices
            .iter()
            .position(|device| device == target_device)
            .unwrap_or(0),
    )
    .unwrap_or(0);

    widgets.selection_guard.set(true);
    if current != devices {
        let additions: Vec<&str> = devices.iter().map(String::as_str).collect();
        widgets
            .device_model
            .splice(0, widgets.device_model.n_items(), &additions);
    }
    if devices.is_empty() {
        widgets.device_selector.set_selected(u32::MAX);
        widgets.device_selector.set_sensitive(false);
    } else {
        widgets.device_selector.set_selected(target);
        widgets.device_selector.set_sensitive(true);
    }
    widgets.selection_guard.set(false);
}

#[allow(clippy::too_many_lines)]
fn update_snapshot_values(model: &App, widgets: &AppWidgets) {
    let snapshot = &model.snapshot;
    set_metric_tone(
        widgets,
        "overview.offset",
        if snapshot
            .offset_nanoseconds
            .is_some_and(|offset| offset.unsigned_abs() < 1_000)
        {
            "good"
        } else {
            "warning"
        },
    );
    set_metric_tone(
        widgets,
        "overview.window",
        if snapshot.sample_window_nanoseconds.is_some() {
            "blue"
        } else {
            "warning"
        },
    );
    let gnss_tone = if snapshot.gnss_locked {
        "good"
    } else {
        "warning"
    };
    set_metric_tone(widgets, "overview.gnss", gnss_tone);
    set_metric_tone(widgets, "gnss.state", gnss_tone);
    set_value(
        widgets,
        "overview.offset",
        &format_duration(snapshot.offset_nanoseconds, true),
    );
    set_value(widgets, "overview.gnss", &available(&snapshot.gnss_state));
    set_value(
        widgets,
        "overview.window",
        &format_duration(snapshot.sample_window_nanoseconds, false),
    );
    let tai = snapshot.utc_tai_offset.map_or_else(
        || "Unavailable".to_owned(),
        |offset| {
            let source = if snapshot.utc_tai_offset_from_kernel {
                "kernel"
            } else {
                "Time Card"
            };
            format!("{offset} s ({source})")
        },
    );
    set_value(widgets, "overview.tai", &tai);
    set_value(
        widgets,
        "overview.phc",
        &format_timestamp(snapshot.phc_utc_nanoseconds),
    );
    set_value(
        widgets,
        "overview.system",
        &format_timestamp(
            (snapshot.system_utc_nanoseconds != 0).then_some(snapshot.system_utc_nanoseconds),
        ),
    );
    set_value(
        widgets,
        "overview.clock_source",
        &available(&snapshot.clock_source),
    );
    set_value(
        widgets,
        "overview.fpga_offset",
        &format_duration(snapshot.clock_offset_nanoseconds, true),
    );
    set_value(
        widgets,
        "overview.drift",
        &snapshot.clock_drift_ppb.map_or_else(
            || "Unavailable".to_owned(),
            |drift| format!("{drift:+} ppb"),
        ),
    );
    set_value(
        widgets,
        "overview.method",
        &available(&snapshot.timestamp_method),
    );

    set_value(widgets, "identity.device", &available(&snapshot.device_id));
    set_value(
        widgets,
        "identity.profile",
        &available(&snapshot.board_profile),
    );
    set_value(widgets, "identity.pci", &pci_identity(snapshot));
    set_value(
        widgets,
        "identity.serial",
        &available(&snapshot.serial_number),
    );
    set_value(widgets, "identity.ptp", &available(&snapshot.ptp_device));
    set_value(widgets, "identity.pps", &available(&snapshot.pps_device));
    set_value(widgets, "identity.sysfs", &available(&snapshot.sysfs_path));

    set_value(
        widgets,
        "timing.sma_count",
        &format!("{} detected", snapshot.sma_states.len()),
    );
    set_value(
        widgets,
        "timing.generator_count",
        &format!("{} detected", snapshot.generator_states.len()),
    );
    set_value(
        widgets,
        "timing.counter_count",
        &format!("{} detected", snapshot.frequency_counter_states.len()),
    );
    set_value(
        widgets,
        "timing.fpga_count",
        &format!("{} readable", snapshot.fpga_engine_states.len()),
    );
    set_value(
        widgets,
        "timing.image_contract",
        &available(&snapshot.optional_image_contract),
    );

    set_value(
        widgets,
        "sensors.profile",
        &available(&snapshot.board_profile),
    );
    set_value(
        widgets,
        "sensors.count",
        &format!("{} readings", snapshot.sensor_states.len()),
    );
    set_value(
        widgets,
        "leds.count",
        &format!("{} detected", snapshot.led_states.len()),
    );

    set_value(widgets, "gnss.state", &available(&snapshot.gnss_state));
    let protocol = if snapshot.tod_protocol.is_empty() {
        "Unavailable".to_owned()
    } else if snapshot.tod_baud_rate.is_empty() {
        snapshot.tod_protocol.clone()
    } else {
        format!(
            "{} / {} baud",
            snapshot.tod_protocol, snapshot.tod_baud_rate
        )
    };
    set_value(widgets, "gnss.protocol", &protocol);
    set_value(widgets, "gnss.source", &available(&snapshot.clock_source));
    set_value(widgets, "gnss.primary", &available(&snapshot.tty_gnss));
    set_value(widgets, "gnss.secondary", &available(&snapshot.tty_gnss2));
    set_value(widgets, "gnss.atomic", &available(&snapshot.tty_mac));
    set_value(widgets, "gnss.nmea", &available(&snapshot.tty_nmea));
    set_value(widgets, "gnss.i2c", &available(&snapshot.i2c_device));
    set_value(widgets, "gnss.mro50", &available(&snapshot.mro50_device));
}

#[allow(clippy::too_many_lines)]
fn update_oscillatord_values(model: &App, widgets: &AppWidgets) {
    let endpoint = display_endpoint(
        &model.config.oscillatord_host,
        model.config.oscillatord_port,
    );
    set_value(widgets, "osc.endpoint", &endpoint);
    set_metric_tone(
        widgets,
        "osc.service",
        if model.oscillatord.is_some() {
            "good"
        } else {
            "warning"
        },
    );

    let Some(telemetry) = &model.oscillatord else {
        set_value(
            widgets,
            "osc.policy",
            "Service unavailable; this client remains read-only",
        );
        for id in [
            "osc.service",
            "osc.discipline",
            "osc.gnss",
            "osc.version",
            "osc.action",
            "osc.status",
            "osc.holdover",
            "osc.clock",
            "osc.convergence",
            "osc.oscillator",
            "osc.controls",
            "osc.gnss_detail",
            "osc.antenna",
        ] {
            set_value(widgets, id, "Unavailable");
        }
        widgets.oscillatord_progress.set_fraction(0.0);
        widgets.oscillatord_progress.set_text(Some("Unavailable"));
        return;
    };

    let version = format!("oscillatord {}", available(&telemetry.service_version));
    set_value(widgets, "osc.service", &version);
    set_value(widgets, "osc.version", &version);
    let action = if telemetry.action_requested.is_empty() {
        "Status"
    } else {
        &telemetry.action_requested
    };
    set_value(widgets, "osc.action", action);
    let mut policy = if telemetry.control_enabled {
        "Service controls enabled; this client remains read-only".to_owned()
    } else {
        "Service controls disabled; this client is read-only".to_owned()
    };
    if model.snapshot.available_devices.len() > 1 {
        policy.push_str("; endpoint status is not correlated to the selected card");
    }
    set_value(widgets, "osc.policy", &policy);

    let (status, holdover, convergence, progress) = telemetry.discipline.as_ref().map_or_else(
        || {
            (
                "Unavailable".to_owned(),
                "Unavailable".to_owned(),
                "No discipline telemetry".to_owned(),
                0.0,
            )
        },
        |discipline| {
            let detail = match (
                discipline.current_convergence_count,
                discipline.convergence_threshold,
            ) {
                (Some(current), Some(threshold)) => format!(
                    "{:.1}% ({current}/{threshold} samples)",
                    discipline.convergence_progress
                ),
                _ => format!("{:.1}%", discipline.convergence_progress),
            };
            (
                available(&discipline.status),
                if discipline.ready_for_holdover {
                    "Ready for holdover".to_owned()
                } else {
                    "Not ready for holdover".to_owned()
                },
                detail,
                discipline.convergence_progress,
            )
        },
    );
    set_value(widgets, "osc.discipline", &status);
    set_value(widgets, "osc.status", &status);
    set_value(widgets, "osc.holdover", &holdover);
    set_value(widgets, "osc.convergence", &convergence);
    widgets
        .oscillatord_progress
        .set_fraction((progress / 100.0).clamp(0.0, 1.0));
    widgets.oscillatord_progress.set_text(Some(&convergence));

    set_value(
        widgets,
        "osc.clock",
        &format!(
            "{}; offset {}",
            available(&telemetry.clock_class),
            format_duration(Some(telemetry.clock_offset_nanoseconds), true)
        ),
    );
    set_value(
        widgets,
        "osc.oscillator",
        &format!(
            "{}; {}; {}",
            available(&telemetry.oscillator_model),
            format_oscillator_temperature(telemetry.oscillator_temperature_celsius),
            if telemetry.oscillator_locked {
                "locked"
            } else {
                "unlocked"
            }
        ),
    );
    set_value(
        widgets,
        "osc.controls",
        &format!(
            "fine {}; coarse {}; control {}",
            optional_i64(telemetry.fine_control),
            optional_i64(telemetry.coarse_control),
            if telemetry.control_enabled {
                "enabled"
            } else {
                "disabled"
            }
        ),
    );
    let satellites = format_satellite_count(telemetry.satellites);
    let gnss = format!(
        "{}; {satellites}",
        if telemetry.gnss_fix_ok {
            "Fix OK"
        } else {
            "No valid fix"
        }
    );
    set_value(widgets, "osc.gnss", &gnss);
    let mut gnss_detail = vec![
        format!("fix {}", optional_i64(telemetry.gnss_fix)),
        format!(
            "accuracy {}",
            format_duration(telemetry.time_accuracy_nanoseconds, false)
        ),
        format!(
            "survey error {}",
            telemetry
                .survey_position_error_meters
                .map_or_else(|| "Unavailable".to_owned(), |value| format!("{value:.3} m"))
        ),
    ];
    if let Some(leap_seconds) = telemetry.leap_seconds {
        let mut leap_detail = format!("GPS-UTC {leap_seconds} s");
        if let Some(change) = telemetry.leap_second_change.filter(|change| *change != -10) {
            leap_detail.push_str(&format!(", pending change {change}"));
        }
        gnss_detail.push(leap_detail);
    }
    set_value(widgets, "osc.gnss_detail", &gnss_detail.join("; "));
    set_value(
        widgets,
        "osc.antenna",
        &format!(
            "power {}; status {}",
            optional_i64(telemetry.antenna_power),
            optional_i64(telemetry.antenna_status)
        ),
    );
}

fn set_value(widgets: &AppWidgets, id: &'static str, value: &str) {
    if let Some(label) = widgets.values.get(id) {
        label.set_label(value);
        label.set_tooltip_text(Some(value));
    }
}

fn set_metric_tone(widgets: &AppWidgets, id: &'static str, tone: &str) {
    if let Some(label) = widgets.values.get(id) {
        for class in ["blue", "violet", "good", "warning"] {
            label.remove_css_class(class);
        }
        if !tone.is_empty() {
            label.add_css_class(tone);
        }
    }
}

fn update_state_list_if_changed(
    container: &gtk::Box,
    cache: &mut StateListCache,
    states: &[String],
    empty_message: &str,
) {
    if states.is_empty() {
        if cache.rendered_empty {
            return;
        }
        clear_box(container);
        let empty = left_label(empty_message, "muted");
        empty.set_wrap(true);
        empty.set_margin_top(8);
        container.append(&empty);
        cache.keys.clear();
        cache.value_labels.clear();
        cache.rendered_empty = true;
        return;
    }

    let parsed: Vec<(&str, &str)> = states
        .iter()
        .map(|state| {
            state
                .split_once(" | ")
                .map_or((state.as_str(), ""), |(key, value)| (key, value))
        })
        .collect();
    let keys: Vec<String> = parsed.iter().map(|(key, _)| (*key).to_owned()).collect();
    if cache.keys == keys && cache.value_labels.len() == parsed.len() {
        for (label, (_, value)) in cache.value_labels.iter().zip(&parsed) {
            if label.label().as_str() != *value {
                label.set_label(value);
                label.set_tooltip_text(Some(value));
            }
        }
        return;
    }

    clear_box(container);
    cache.keys = keys;
    cache.value_labels.clear();
    cache.rendered_empty = false;
    for (key, value) in parsed {
        let row = gtk::Box::new(gtk::Orientation::Horizontal, 18);
        row.add_css_class("value-row");
        let key = left_label(key, "value-key");
        key.set_hexpand(true);
        let value_label = gtk::Label::new(Some(value));
        value_label.set_halign(gtk::Align::End);
        value_label.set_xalign(1.0);
        value_label.set_selectable(true);
        value_label.set_wrap(true);
        value_label.set_tooltip_text(Some(value));
        value_label.add_css_class("value-data");
        row.append(&key);
        row.append(&value_label);
        container.append(&row);
        cache.value_labels.push(value_label);
    }
}

fn update_capabilities_if_changed(
    container: &gtk::Box,
    rendered: &mut Option<Vec<String>>,
    capabilities: &[String],
) {
    if rendered.as_deref() != Some(capabilities) {
        populate_chips(container, capabilities);
        *rendered = Some(capabilities.to_vec());
    }
}

fn update_log_if_changed(
    container: &gtk::Box,
    rendered: &mut Option<Vec<String>>,
    lines: &[String],
) {
    if rendered.as_deref() != Some(lines) {
        populate_log(container, lines);
        *rendered = Some(lines.to_vec());
    }
}

fn populate_chips(container: &gtk::Box, capabilities: &[String]) {
    clear_box(container);
    if capabilities.is_empty() {
        container.append(&left_label("No additional capabilities reported", "muted"));
        return;
    }
    for capability in capabilities {
        let chip = left_label(capability, "read-only-chip");
        chip.set_margin_top(2);
        chip.set_margin_bottom(2);
        container.append(&chip);
    }
}

fn populate_log(container: &gtk::Box, lines: &[String]) {
    clear_box(container);
    if lines.is_empty() {
        container.append(&left_label("No session records", "muted"));
        return;
    }
    for line in lines {
        let label = left_label(line, "log-line");
        label.set_ellipsize(gtk::pango::EllipsizeMode::End);
        label.set_tooltip_text(Some(line));
        container.append(&label);
    }
}

fn clear_box(container: &gtk::Box) {
    while let Some(child) = container.first_child() {
        container.remove(&child);
    }
}

fn append_bounded(history: &mut Vec<f64>, value: f64, capacity: usize) {
    history.push(value);
    if history.len() > capacity {
        history.remove(0);
    }
}

fn history_summary(samples: &[f64], unit: &str) -> String {
    let Some(latest) = samples.last() else {
        return "No timing samples".to_owned();
    };
    let (minimum, maximum) = samples.iter().fold(
        (f64::INFINITY, f64::NEG_INFINITY),
        |(minimum, maximum), sample| (minimum.min(*sample), maximum.max(*sample)),
    );
    format!(
        "{} samples; latest {latest:.0} {unit}; range {minimum:.0} to {maximum:.0} {unit}",
        samples.len()
    )
}

fn page_heading(
    page: Page,
    snapshot: &TimeCardSnapshot,
    config: &AppConfig,
) -> (&'static str, String) {
    let title = match page {
        Page::Overview => "Precision timing overview",
        Page::TimingIo => "FPGA timing I/O",
        Page::Sensors => "R4006 sensors and status LEDs",
        Page::Gnss => "GNSS and serial endpoints",
        Page::Oscillatord => "Oscillator discipline",
    };
    let subtitle = if page == Page::Oscillatord {
        format!(
            "Endpoint  {}",
            display_endpoint(&config.oscillatord_host, config.oscillatord_port)
        )
    } else {
        format!(
            "{}  |  {}",
            available(&snapshot.device_id),
            pci_identity(snapshot)
        )
    };
    (title, subtitle)
}

fn pci_identity(snapshot: &TimeCardSnapshot) -> String {
    let identity = match (
        snapshot.pci_vendor.is_empty(),
        snapshot.pci_device.is_empty(),
    ) {
        (false, false) => format!("{}:{}", snapshot.pci_vendor, snapshot.pci_device),
        (false, true) => snapshot.pci_vendor.clone(),
        (true, false) => snapshot.pci_device.clone(),
        (true, true) => String::new(),
    };
    match (identity.is_empty(), snapshot.pci_address.is_empty()) {
        (false, false) => format!("{identity} at {}", snapshot.pci_address),
        (false, true) => identity,
        (true, false) => snapshot.pci_address.clone(),
        (true, true) => "Unavailable".to_owned(),
    }
}

fn available(value: &str) -> String {
    if value.trim().is_empty() {
        "Unavailable".to_owned()
    } else {
        value.to_owned()
    }
}

fn optional_i64(value: Option<i64>) -> String {
    value.map_or_else(|| "Unavailable".to_owned(), |value| value.to_string())
}

fn format_oscillator_temperature(value: f64) -> String {
    if value <= -273.15 {
        "temperature unavailable".to_owned()
    } else {
        format!("{value:.2} C")
    }
}

fn format_satellite_count(value: i64) -> String {
    if value >= 0 {
        format!("{value} satellites")
    } else {
        "satellites unavailable".to_owned()
    }
}

#[cfg(test)]
mod tests {
    use super::{format_oscillator_temperature, format_satellite_count, history_summary};

    #[test]
    fn hides_oscillatord_initialization_sentinels() {
        assert_eq!(
            format_oscillator_temperature(-400.0),
            "temperature unavailable"
        );
        assert_eq!(format_satellite_count(-1), "satellites unavailable");
        assert_eq!(format_oscillator_temperature(42.5), "42.50 C");
        assert_eq!(format_satellite_count(8), "8 satellites");
    }

    #[test]
    fn summarizes_chart_values_for_accessibility() {
        assert_eq!(history_summary(&[], "nanoseconds"), "No timing samples");
        assert_eq!(
            history_summary(&[-4.0, 8.0], "nanoseconds"),
            "2 samples; latest 8 nanoseconds; range -4 to 8 nanoseconds"
        );
    }
}
