//! Engine: owns the board link and the state the UI shows. Runs on its own thread for as long as
//! fenixOS runs, with or without a window (PLAN §3.5). The UI only reads a snapshot and listens
//! for `state` / `log` events, which are sent only while the window exists.

use std::sync::mpsc::{self, Receiver, Sender};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use fenix_link::{Event, Link, LinkOptions};
use fenix_proto::Msg;
use serde::Serialize;
use tauri::{AppHandle, Emitter, Manager};

use crate::apps::{AppView, Apps, Cmd};
use crate::logs::{Logs, Source};
use crate::registry::Manifest;

const TIME_SYNC_EVERY: Duration = Duration::from_secs(3600);

#[derive(Debug, Clone, Serialize, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum Status {
    Searching,
    Connected,
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Board {
    pub port: String,
    pub firmware: String,
    pub proto: u8,
    pub width: u16,
    pub height: u16,
    pub rotation: u8,
    pub mac: String,
    /// Uptime at the handshake plus the time since, so the UI can count on its own.
    pub uptime_s: u64,
    pub free_heap: u32,
    pub min_heap: u32,
    pub fps_x10: u16,
    pub apps_used_kb: u16,
    pub apps_total_kb: u16,
    pub screen: u8,
    pub app_id: String,
}

/// A CH340 port that did not answer as fenixOS (blank board, other firmware, other protocol).
#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Silent {
    pub port: String,
    pub detail: String,
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct State {
    pub status: Status,
    pub board: Option<Board>,
    pub silent: Vec<Silent>,
    pub app_version: String,
    /// Board firmware equals this app's version (PLAN §3.8); false while searching.
    pub firmware_ok: bool,
    pub apps: Vec<AppView>,
}

pub struct Engine {
    state: Mutex<State>,
    pub logs: Logs,
    link: Link,
    connected_at: Mutex<Option<(Instant, u32)>>,
    apps: Mutex<Apps>,
    inbox: Mutex<Sender<Cmd>>,
    inbox_rx: Mutex<Option<Receiver<Cmd>>>,
    /// The registry as last read by Add app.
    pub registry: Mutex<Vec<Manifest>>,
}

impl Engine {
    pub fn start(app: AppHandle, logs: Logs) -> Arc<Engine> {
        let (tx, rx) = mpsc::channel();
        let cfg = crate::config::Config::load();
        let link = Link::spawn(LinkOptions {
            port: cfg.port.clone(),
            ..LinkOptions::default()
        });
        let apps = Apps::new(link.sender(), tx.clone());
        let engine = Arc::new(Engine {
            state: Mutex::new(State {
                status: Status::Searching,
                board: None,
                silent: Vec::new(),
                app_version: env!("CARGO_PKG_VERSION").into(),
                firmware_ok: false,
                apps: apps.views(),
            }),
            logs,
            link,
            connected_at: Mutex::new(None),
            apps: Mutex::new(apps),
            inbox: Mutex::new(tx),
            inbox_rx: Mutex::new(Some(rx)),
            registry: Mutex::new(Vec::new()),
        });
        engine.log(
            &app,
            Source::Pc,
            false,
            format!("fenixOS v{} started", env!("CARGO_PKG_VERSION")),
        );
        let e = engine.clone();
        thread::Builder::new()
            .name("fenix-engine".into())
            .spawn(move || e.run(app))
            .expect("spawn engine thread");
        engine
    }

    pub fn snapshot(&self) -> State {
        let mut s = self.state.lock().unwrap().clone();
        if let (Some(b), Some((at, up))) = (s.board.as_mut(), *self.connected_at.lock().unwrap()) {
            b.uptime_s = up as u64 + at.elapsed().as_secs();
        }
        s
    }

    pub fn send(&self, m: Msg) {
        self.link.send(m);
    }

    /// Hand a change to the engine thread.
    pub fn post(&self, c: Cmd) {
        let _ = self.inbox.lock().unwrap().send(c);
    }

    /// Read something from the app manager (config, installed versions).
    pub fn with_apps<T>(&self, f: impl FnOnce(&mut Apps) -> T) -> T {
        f(&mut self.apps.lock().unwrap())
    }

    pub fn log(&self, app: &AppHandle, source: Source, problem: bool, text: impl Into<String>) {
        let line = self.logs.add(source, problem, text);
        if window_open(app) {
            let _ = app.emit_to("main", "log", line);
        }
    }

    fn run(self: Arc<Self>, app: AppHandle) {
        let mut next_sync = Instant::now() + TIME_SYNC_EVERY;
        let inbox = self
            .inbox_rx
            .lock()
            .unwrap()
            .take()
            .expect("engine runs once");
        loop {
            let mut changed = false;
            while let Ok(c) = inbox.try_recv() {
                changed |= self.apps.lock().unwrap().command(c);
            }
            // Poll fast only while an install streams chunks; otherwise stay mostly asleep.
            let wait = if self.apps.lock().unwrap().busy() {
                5
            } else {
                100
            };
            if let Some(e) = self.link.recv_event(Duration::from_millis(wait)) {
                changed |= self.handle(&app, e);
            }
            changed |= self.apps.lock().unwrap().tick();
            self.flush_app_logs(&app);
            if self.state.lock().unwrap().status == Status::Connected && Instant::now() >= next_sync
            {
                next_sync = Instant::now() + TIME_SYNC_EVERY;
                self.sync_time(&app);
            }
            if changed {
                let views = self.apps.lock().unwrap().views();
                self.state.lock().unwrap().apps = views;
                crate::tray::update(&app, &self.snapshot());
                if window_open(&app) {
                    let _ = app.emit_to("main", "state", self.snapshot());
                }
            }
        }
    }

    fn flush_app_logs(&self, app: &AppHandle) {
        let lines = std::mem::take(&mut self.apps.lock().unwrap().logs);
        for (source, problem, text) in lines {
            self.log(app, source, problem, text);
        }
    }

    fn sync_time(&self, app: &AppHandle) {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_secs())
            .unwrap_or(0);
        self.link.send(Msg::TimeSet(now as u32));
        self.log(app, Source::Pc, false, "Time synced");
    }

    /// Apply one link event. True when the state the UI shows changed.
    fn handle(&self, app: &AppHandle, e: Event) -> bool {
        match e {
            Event::Connected { port, info } => {
                let fw = format!("{}.{}.{}", info.fw.0, info.fw.1, info.fw.2);
                let firmware_ok = fw == env!("CARGO_PKG_VERSION");
                *self.connected_at.lock().unwrap() = Some((Instant::now(), info.uptime_s));
                {
                    let mut s = self.state.lock().unwrap();
                    s.status = Status::Connected;
                    s.firmware_ok = firmware_ok;
                    s.silent.clear();
                    s.board = Some(Board {
                        port: port.clone(),
                        firmware: fw.clone(),
                        proto: info.proto,
                        width: info.width,
                        height: info.height,
                        rotation: info.rotation,
                        mac: info
                            .mac
                            .iter()
                            .map(|b| format!("{b:02x}"))
                            .collect::<Vec<_>>()
                            .join(":"),
                        uptime_s: info.uptime_s as u64,
                        free_heap: info.free_heap,
                        min_heap: info.free_heap,
                        fps_x10: 0,
                        apps_used_kb: info.apps_used_kb,
                        apps_total_kb: info.apps_total_kb,
                        screen: 0,
                        app_id: String::new(),
                    });
                }
                self.log(app, Source::Pc, false, format!("Board found on {port}"));
                self.log(
                    app,
                    Source::Board,
                    false,
                    format!(
                        "fenixOS v{fw}, boot reason: {}, up {} s",
                        reset_reason(info.reset_reason),
                        info.uptime_s
                    ),
                );
                self.sync_time(app);
                self.apps.lock().unwrap().board_connected(firmware_ok);
                true
            }
            Event::Disconnected { reason } => {
                self.apps.lock().unwrap().board_disconnected();
                let was = {
                    let mut s = self.state.lock().unwrap();
                    let was = s.status == Status::Connected;
                    s.status = Status::Searching;
                    s.firmware_ok = false;
                    s.board = None;
                    was
                };
                *self.connected_at.lock().unwrap() = None;
                if was {
                    // A restart is not a problem by itself: the next boot line says why it restarted.
                    let problem = reason != "board restarted";
                    self.log(
                        app,
                        Source::Pc,
                        problem,
                        format!("Board disconnected ({reason})"),
                    );
                }
                was
            }
            Event::Unresponsive { port, detail } => {
                self.log(
                    app,
                    Source::Pc,
                    true,
                    format!("{port} did not answer as fenixOS: {detail}"),
                );
                let mut s = self.state.lock().unwrap();
                s.silent.retain(|x| x.port != port);
                s.silent.push(Silent { port, detail });
                true
            }
            Event::Msg(Msg::Stats(st)) => {
                let mut s = self.state.lock().unwrap();
                match s.board.as_mut() {
                    Some(b) => {
                        b.free_heap = st.free_heap;
                        b.min_heap = st.min_heap;
                        b.fps_x10 = st.fps_x10;
                        true
                    }
                    None => false,
                }
            }
            Event::Msg(Msg::BoardState {
                screen,
                rotation,
                app_id,
                ..
            }) => {
                let mut s = self.state.lock().unwrap();
                match s.board.as_mut() {
                    Some(b) => {
                        b.screen = screen;
                        b.rotation = rotation;
                        b.app_id = app_id;
                        true
                    }
                    None => false,
                }
            }
            Event::Msg(Msg::Log { level, text }) => {
                self.log(app, Source::Board, level <= 2, text);
                false
            }
            Event::Msg(Msg::Error { code, text }) => {
                self.log(app, Source::Board, true, format!("Error {code}: {text}"));
                false
            }
            Event::Msg(m) => self.apps.lock().unwrap().board_msg(&m),
            _ => false,
        }
    }
}

pub fn window_open(app: &AppHandle) -> bool {
    app.get_webview_window("main").is_some()
}

/// `esp_reset_reason_t` in plain words.
fn reset_reason(r: u8) -> &'static str {
    match r {
        1 => "power on",
        2 => "reset pin",
        3 => "restart",
        4 => "crash",
        5..=7 => "watchdog",
        8 => "wake from sleep",
        9 => "low voltage",
        _ => "unknown",
    }
}
