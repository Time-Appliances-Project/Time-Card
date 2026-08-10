use std::io::{self, IsTerminal, Write};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::mpsc::{self, Receiver, SyncSender, TryRecvError, TrySendError};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use chrono::Utc;
use crossterm::cursor::{Hide, Show};
use crossterm::event::{self, Event};
use crossterm::execute;
use crossterm::terminal::{
    EnterAlternateScreen, LeaveAlternateScreen, disable_raw_mode, enable_raw_mode,
};
use directories::UserDirs;
use ratatui::Terminal;
use ratatui::backend::CrosstermBackend;

use crate::oscillatord::{OscillatordTelemetry, display_endpoint};
use crate::{LinuxTimeCardBackend, MockTimeCardBackend, TimeCardBackend, TimeCardSnapshot};

use super::render::{draw, plain_text};
use super::state::{TuiAction, TuiState};
use super::{TuiConfig, TuiPage};

const CARD_POLL_INTERVAL: Duration = Duration::from_secs(1);
const OSCILLATORD_POLL_INTERVAL: Duration = Duration::from_secs(5);
const OSCILLATORD_TIMEOUT: Duration = Duration::from_secs(1);
const EVENT_POLL_LIMIT: Duration = Duration::from_millis(50);

#[derive(Debug, thiserror::Error)]
pub enum TuiError {
    #[error("{0}")]
    Io(#[from] io::Error),
    #[error("interactive mode requires terminal input and output; use --plain for scripts")]
    NotATerminal,
    #[error("could not start the {0} telemetry worker: {1}")]
    WorkerStart(&'static str, io::Error),
}

/// Runs either the interactive terminal application or one plain snapshot.
///
/// # Errors
///
/// Returns a terminal, worker-start, telemetry-output, or session setup error.
pub fn run(config: TuiConfig) -> Result<(), TuiError> {
    if config.plain {
        run_plain(&config)
    } else {
        run_interactive(config)
    }
}

fn run_plain(config: &TuiConfig) -> Result<(), TuiError> {
    let endpoint = display_endpoint(&config.oscillatord_host, config.oscillatord_port);
    let mut state = TuiState::new(config.page, endpoint);
    state.card_busy = true;
    let mut backend = create_backend(config);
    state.apply_snapshot(backend.read_snapshot());

    if config.page == TuiPage::Oscillatord {
        state.oscillatord_busy = true;
        let result = OscillatordTelemetry::poll(
            &config.oscillatord_host,
            config.oscillatord_port,
            OSCILLATORD_TIMEOUT,
        )
        .map_err(|error| error.to_string());
        state.apply_oscillatord(result);
    }

    io::stdout().write_all(plain_text(&state).as_bytes())?;
    Ok(())
}

fn run_interactive(config: TuiConfig) -> Result<(), TuiError> {
    if !io::stdin().is_terminal() || !io::stdout().is_terminal() {
        return Err(TuiError::NotATerminal);
    }

    let endpoint = display_endpoint(&config.oscillatord_host, config.oscillatord_port);
    let backend = create_backend(&config);
    let workers = Workers::start(
        backend,
        config.oscillatord_host.clone(),
        config.oscillatord_port,
    )?;
    let mut application = InteractiveApplication::new(config.page, endpoint, workers);
    let _signals = SignalHandlers::install()?;
    let _terminal_session = TerminalSession::enter()?;
    let backend = CrosstermBackend::new(io::stdout());
    let mut terminal = Terminal::new(backend)?;
    application.run(&mut terminal, config.quit_after.map(Duration::from_millis))
}

fn create_backend(config: &TuiConfig) -> Box<dyn TimeCardBackend> {
    if config.mock {
        Box::new(MockTimeCardBackend::new())
    } else {
        Box::new(LinuxTimeCardBackend::new(
            config.sysfs_root.clone(),
            config.hwmon_root.clone(),
            config.iio_root.clone(),
            config.leds_root.clone(),
        ))
    }
}

#[derive(Clone, Debug)]
enum CardRequest {
    Refresh,
    Select(String),
}

#[derive(Debug)]
enum CardCommand {
    Read {
        generation: u64,
        request: CardRequest,
    },
    Stop,
}

#[derive(Debug)]
enum OscillatordCommand {
    Read { generation: u64 },
    Stop,
}

#[derive(Debug)]
enum WorkerResult {
    Card {
        generation: u64,
        snapshot: Box<TimeCardSnapshot>,
    },
    Oscillatord {
        generation: u64,
        result: Box<Result<OscillatordTelemetry, String>>,
    },
}

struct Workers {
    card: SyncSender<CardCommand>,
    oscillatord: SyncSender<OscillatordCommand>,
    results: Receiver<WorkerResult>,
    card_thread: Option<JoinHandle<()>>,
    oscillatord_thread: Option<JoinHandle<()>>,
}

impl Workers {
    fn start(
        mut backend: Box<dyn TimeCardBackend>,
        oscillatord_host: String,
        oscillatord_port: u16,
    ) -> Result<Self, TuiError> {
        let (results, result_receiver) = mpsc::channel();
        let (card_sender, card_receiver) = mpsc::sync_channel(1);
        let card_results = results.clone();
        let card_thread = thread::Builder::new()
            .name("timecard-tui-card".to_owned())
            .spawn(move || {
                while let Ok(command) = card_receiver.recv() {
                    match command {
                        CardCommand::Read {
                            generation,
                            request,
                        } => {
                            if let CardRequest::Select(device) = request {
                                backend.set_selected_device(&device);
                            }
                            let snapshot = backend.read_snapshot();
                            if card_results
                                .send(WorkerResult::Card {
                                    generation,
                                    snapshot: Box::new(snapshot),
                                })
                                .is_err()
                            {
                                break;
                            }
                        }
                        CardCommand::Stop => break,
                    }
                }
            })
            .map_err(|error| TuiError::WorkerStart("card", error))?;

        let (oscillatord_sender, oscillatord_receiver) = mpsc::sync_channel(1);
        let oscillatord_thread = match thread::Builder::new()
            .name("timecard-tui-oscillatord".to_owned())
            .spawn(move || {
                while let Ok(command) = oscillatord_receiver.recv() {
                    match command {
                        OscillatordCommand::Read { generation } => {
                            let result = OscillatordTelemetry::poll(
                                &oscillatord_host,
                                oscillatord_port,
                                OSCILLATORD_TIMEOUT,
                            )
                            .map_err(|error| error.to_string());
                            if results
                                .send(WorkerResult::Oscillatord {
                                    generation,
                                    result: Box::new(result),
                                })
                                .is_err()
                            {
                                break;
                            }
                        }
                        OscillatordCommand::Stop => break,
                    }
                }
            }) {
            Ok(worker) => worker,
            Err(error) => {
                let _ = card_sender.send(CardCommand::Stop);
                let _ = card_thread.join();
                return Err(TuiError::WorkerStart("oscillatord", error));
            }
        };

        Ok(Self {
            card: card_sender,
            oscillatord: oscillatord_sender,
            results: result_receiver,
            card_thread: Some(card_thread),
            oscillatord_thread: Some(oscillatord_thread),
        })
    }

    fn shutdown(&mut self, card_idle: bool, oscillatord_idle: bool) {
        let _ = self.card.try_send(CardCommand::Stop);
        let _ = self.oscillatord.try_send(OscillatordCommand::Stop);

        if let Some(worker) = self.card_thread.take()
            && card_idle
        {
            let _ = worker.join();
        }
        if let Some(worker) = self.oscillatord_thread.take()
            && oscillatord_idle
        {
            let _ = worker.join();
        }
    }
}

impl Drop for Workers {
    fn drop(&mut self) {
        self.shutdown(false, false);
    }
}

struct InteractiveApplication {
    state: TuiState,
    workers: Workers,
    next_generation: u64,
    active_card: Option<u64>,
    pending_card: Option<CardRequest>,
    active_oscillatord: Option<u64>,
    pending_oscillatord: bool,
}

impl InteractiveApplication {
    fn new(page: TuiPage, endpoint: String, workers: Workers) -> Self {
        Self {
            state: TuiState::new(page, endpoint),
            workers,
            next_generation: 1,
            active_card: None,
            pending_card: None,
            active_oscillatord: None,
            pending_oscillatord: false,
        }
    }

    fn run(
        &mut self,
        terminal: &mut Terminal<CrosstermBackend<io::Stdout>>,
        quit_after: Option<Duration>,
    ) -> Result<(), TuiError> {
        let started = Instant::now();
        let quit_deadline = quit_after.and_then(|limit| started.checked_add(limit));
        let mut next_card_poll = started + CARD_POLL_INTERVAL;
        let mut next_oscillatord_poll = started + OSCILLATORD_POLL_INTERVAL;
        let mut dirty = true;
        self.request_card(CardRequest::Refresh);
        self.request_oscillatord();

        loop {
            dirty |= self.drain_results();
            if termination_requested() {
                break;
            }
            if quit_after.is_some_and(|limit| started.elapsed() >= limit) {
                break;
            }

            let now = Instant::now();
            if now >= next_card_poll {
                self.request_card(CardRequest::Refresh);
                next_card_poll = advance_deadline(next_card_poll, CARD_POLL_INTERVAL, now);
                dirty = true;
            }
            if now >= next_oscillatord_poll {
                self.request_oscillatord();
                next_oscillatord_poll =
                    advance_deadline(next_oscillatord_poll, OSCILLATORD_POLL_INTERVAL, now);
                dirty = true;
            }

            if dirty {
                terminal.draw(|frame| draw(frame, &mut self.state))?;
                dirty = false;
            }

            let timeout = event_timeout(next_card_poll, next_oscillatord_poll, quit_deadline);
            match event::poll(timeout) {
                Ok(true) => {
                    let event = match event::read() {
                        Ok(event) => event,
                        Err(error) if error.kind() == io::ErrorKind::Interrupted => {
                            if termination_requested() {
                                break;
                            }
                            continue;
                        }
                        Err(error) => return Err(error.into()),
                    };
                    dirty = true;
                    if let Event::Key(key) = event {
                        let action = self.state.handle_key(key);
                        if self.apply_action(action)? {
                            break;
                        }
                    }
                }
                Ok(false) => {}
                Err(error) if error.kind() == io::ErrorKind::Interrupted => {
                    if termination_requested() {
                        break;
                    }
                }
                Err(error) => return Err(error.into()),
            }
        }
        Ok(())
    }

    fn apply_action(&mut self, action: TuiAction) -> Result<bool, TuiError> {
        match action {
            TuiAction::None => {}
            TuiAction::Quit => return Ok(true),
            TuiAction::RefreshCard => self.request_card(CardRequest::Refresh),
            TuiAction::RefreshOscillatord => self.request_oscillatord(),
            TuiAction::SelectDevice(device) => {
                self.state.status_message = format!("Selecting {device}");
                self.request_card(CardRequest::Select(device));
            }
            TuiAction::ExportLog => self.export_log(),
            TuiAction::ClearLog => self.state.clear_log(),
        }
        Ok(false)
    }

    fn request_card(&mut self, request: CardRequest) {
        if self.active_card.is_some() {
            match (&self.pending_card, &request) {
                (Some(CardRequest::Select(_)), CardRequest::Refresh) => {}
                _ => self.pending_card = Some(request),
            }
            return;
        }
        let generation = self.take_generation();
        match self.workers.card.try_send(CardCommand::Read {
            generation,
            request: request.clone(),
        }) {
            Ok(()) => {
                self.active_card = Some(generation);
                self.state.card_busy = true;
            }
            Err(TrySendError::Full(_)) => self.pending_card = Some(request),
            Err(TrySendError::Disconnected(_)) => {
                self.state.card_busy = false;
                self.state.status_message = "Card telemetry worker stopped".to_owned();
            }
        }
    }

    fn request_oscillatord(&mut self) {
        if self.active_oscillatord.is_some() {
            self.pending_oscillatord = true;
            return;
        }
        let generation = self.take_generation();
        match self
            .workers
            .oscillatord
            .try_send(OscillatordCommand::Read { generation })
        {
            Ok(()) => {
                self.active_oscillatord = Some(generation);
                self.state.oscillatord_busy = true;
            }
            Err(TrySendError::Full(_)) => self.pending_oscillatord = true,
            Err(TrySendError::Disconnected(_)) => {
                self.state.oscillatord_busy = false;
                self.state.status_message = "oscillatord telemetry worker stopped".to_owned();
            }
        }
    }

    fn drain_results(&mut self) -> bool {
        let mut changed = false;
        loop {
            match self.workers.results.try_recv() {
                Ok(WorkerResult::Card {
                    generation,
                    snapshot,
                }) => {
                    if self.active_card == Some(generation) {
                        self.active_card = None;
                        self.state.apply_snapshot(*snapshot);
                        if let Some(request) = self.pending_card.take() {
                            self.request_card(request);
                        }
                        changed = true;
                    }
                }
                Ok(WorkerResult::Oscillatord { generation, result }) => {
                    if self.active_oscillatord == Some(generation) {
                        self.active_oscillatord = None;
                        self.state.apply_oscillatord(*result);
                        if self.pending_oscillatord {
                            self.pending_oscillatord = false;
                            self.request_oscillatord();
                        }
                        changed = true;
                    }
                }
                Err(TryRecvError::Empty | TryRecvError::Disconnected) => break,
            }
        }
        changed
    }

    fn export_log(&mut self) {
        let path = export_path();
        match self.state.log.write_json(&path) {
            Ok(()) => {
                let path = path.to_string_lossy();
                self.state.note_export(Ok(&path));
            }
            Err(error) => {
                let error = error.to_string();
                self.state.note_export(Err(&error));
            }
        }
    }

    fn take_generation(&mut self) -> u64 {
        let generation = self.next_generation;
        self.next_generation = self.next_generation.wrapping_add(1).max(1);
        generation
    }
}

impl Drop for InteractiveApplication {
    fn drop(&mut self) {
        self.workers.shutdown(
            self.active_card.is_none(),
            self.active_oscillatord.is_none(),
        );
    }
}

fn advance_deadline(mut deadline: Instant, interval: Duration, now: Instant) -> Instant {
    while deadline <= now {
        deadline += interval;
    }
    deadline
}

fn event_timeout(card: Instant, oscillatord: Instant, quit: Option<Instant>) -> Duration {
    let now = Instant::now();
    [Some(card), Some(oscillatord), quit]
        .into_iter()
        .flatten()
        .map(|deadline| deadline.saturating_duration_since(now))
        .fold(EVENT_POLL_LIMIT, Duration::min)
}

fn export_path() -> PathBuf {
    let directory = UserDirs::new()
        .and_then(|directories| directories.document_dir().map(PathBuf::from))
        .or_else(|| std::env::current_dir().ok())
        .unwrap_or_else(|| PathBuf::from("."));
    let now = Utc::now();
    let sequence = EXPORT_SEQUENCE.fetch_add(1, Ordering::Relaxed);
    directory.join(format!(
        "timecard-session-{}-{:09}-{}-{sequence}.json",
        now.format("%Y%m%dT%H%M%SZ"),
        now.timestamp_subsec_nanos(),
        std::process::id(),
    ))
}

struct TerminalSession;

impl TerminalSession {
    fn enter() -> io::Result<Self> {
        enable_raw_mode()?;
        let mut output = io::stdout();
        if let Err(error) = execute!(output, EnterAlternateScreen, Hide) {
            let _ = execute!(output, Show, LeaveAlternateScreen);
            let _ = disable_raw_mode();
            return Err(error);
        }
        Ok(Self)
    }
}

impl Drop for TerminalSession {
    fn drop(&mut self) {
        let _ = disable_raw_mode();
        let _ = execute!(io::stdout(), Show, LeaveAlternateScreen);
    }
}

static TERMINATION_REQUESTED: AtomicBool = AtomicBool::new(false);
static EXPORT_SEQUENCE: AtomicU64 = AtomicU64::new(1);

unsafe extern "C" fn termination_signal(_signal: libc::c_int) {
    TERMINATION_REQUESTED.store(true, Ordering::Relaxed);
}

struct SignalHandlers {
    previous_interrupt: libc::sigaction,
    previous_terminate: libc::sigaction,
}

impl SignalHandlers {
    fn install() -> io::Result<Self> {
        TERMINATION_REQUESTED.store(false, Ordering::Relaxed);
        // SAFETY: sigaction is initialized completely before installation. The handler only
        // performs a lock-free atomic store, and both prior dispositions are retained for Drop.
        unsafe {
            let mut action: libc::sigaction = std::mem::zeroed();
            libc::sigemptyset(&mut action.sa_mask);
            action.sa_flags = 0;
            action.sa_sigaction = termination_signal as *const () as usize;

            let mut previous_interrupt: libc::sigaction = std::mem::zeroed();
            if libc::sigaction(libc::SIGINT, &action, &raw mut previous_interrupt) != 0 {
                return Err(io::Error::last_os_error());
            }
            let mut previous_terminate: libc::sigaction = std::mem::zeroed();
            if libc::sigaction(libc::SIGTERM, &action, &raw mut previous_terminate) != 0 {
                let error = io::Error::last_os_error();
                let _ = libc::sigaction(libc::SIGINT, &previous_interrupt, std::ptr::null_mut());
                return Err(error);
            }
            Ok(Self {
                previous_interrupt,
                previous_terminate,
            })
        }
    }
}

impl Drop for SignalHandlers {
    fn drop(&mut self) {
        // SAFETY: these structures were returned by successful sigaction calls in install.
        unsafe {
            let _ = libc::sigaction(libc::SIGINT, &self.previous_interrupt, std::ptr::null_mut());
            let _ = libc::sigaction(
                libc::SIGTERM,
                &self.previous_terminate,
                std::ptr::null_mut(),
            );
        }
    }
}

fn termination_requested() -> bool {
    TERMINATION_REQUESTED.load(Ordering::Relaxed)
}

#[cfg(test)]
mod tests {
    use super::*;

    struct BlockingBackend {
        entered: SyncSender<()>,
    }

    impl TimeCardBackend for BlockingBackend {
        fn backend_name(&self) -> &'static str {
            "blocking test backend"
        }

        fn selected_device(&self) -> &str {
            "ocp0"
        }

        fn set_selected_device(&mut self, _device_id: &str) {}

        fn available_devices(&self) -> Vec<String> {
            vec!["ocp0".to_owned()]
        }

        fn read_snapshot(&mut self) -> TimeCardSnapshot {
            let _ = self.entered.send(());
            loop {
                thread::park();
            }
        }
    }

    #[test]
    fn coalesced_deadline_advances_past_now() {
        let now = Instant::now();
        let deadline = now - Duration::from_secs(3);
        assert!(advance_deadline(deadline, Duration::from_secs(1), now) > now);
    }

    #[test]
    fn event_wait_is_bounded() {
        let far = Instant::now() + Duration::from_secs(60);
        assert!(event_timeout(far, far, None) <= EVENT_POLL_LIMIT);
    }

    #[test]
    fn export_names_are_unique_within_one_second() {
        assert_ne!(export_path(), export_path());
    }

    #[test]
    fn shutdown_does_not_join_a_blocked_card_read() {
        let (entered, receiver) = mpsc::sync_channel(1);
        let workers = Workers::start(
            Box::new(BlockingBackend { entered }),
            "127.0.0.1".to_owned(),
            2958,
        )
        .unwrap();
        let mut application =
            InteractiveApplication::new(TuiPage::Overview, "127.0.0.1:2958".to_owned(), workers);
        application.request_card(CardRequest::Refresh);
        receiver.recv_timeout(Duration::from_secs(1)).unwrap();

        let started = Instant::now();
        drop(application);
        assert!(started.elapsed() < Duration::from_millis(200));
    }

    #[test]
    fn plain_mock_contains_selected_card_without_terminal_setup() {
        let config = TuiConfig {
            mock: true,
            sysfs_root: PathBuf::from("/unused"),
            hwmon_root: PathBuf::from("/unused"),
            iio_root: PathBuf::from("/unused"),
            leds_root: PathBuf::from("/unused"),
            oscillatord_host: "127.0.0.1".to_owned(),
            oscillatord_port: 2958,
            page: TuiPage::Overview,
            plain: true,
            quit_after: None,
        };
        let mut backend = create_backend(&config);
        let snapshot = backend.read_snapshot();
        assert_eq!(snapshot.device_id, "mock0");
    }
}
