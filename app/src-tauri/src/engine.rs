//! Engine: owns the board link and the state the UI shows. Runs on its own thread for as long as
//! fenixOS runs, with or without a window (PLAN §3.5). The UI only reads a snapshot and listens
//! for `state` / `log` events, which are sent only while the window exists.

use std::sync::mpsc::{self, Receiver, Sender};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use fenix_link::{Event, Link, LinkOptions, StreamCanvas};
use fenix_proto::Msg;
use serde::Serialize;
use tauri::{AppHandle, Emitter, Manager};

use crate::apps::{AppView, Apps, Cmd};
use crate::logs::{Logs, Source};
use crate::registry::Manifest;

const TIME_SYNC_EVERY: Duration = Duration::from_secs(3600);
const MIRROR_FPS: u8 = 10;
/// The mirror stops when the Board page has not asked for a frame for this long (page left,
/// window closed).
const MIRROR_IDLE: Duration = Duration::from_secs(2);

/// Live copy of the board screen for the Board page (artboard 11).
pub struct Mirror {
    pub canvas: StreamCanvas,
    /// Bumped on every completed rectangle; the UI asks "anything newer than N?".
    pub version: u64,
    on: bool,
    last_poll: Instant,
}

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
    /// When the board booted, in Unix ms (PC clock), so the UI counts the uptime on its own.
    pub booted_at_ms: u64,
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
    /// Version of the firmware bundled with this app, if any (`resources/firmware`).
    pub bundled_firmware: Option<String>,
    /// A firmware update in progress or just finished (artboard 12).
    pub update: Option<Update>,
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Update {
    /// `writing`, `restarting`, `done` or `failed`.
    pub stage: String,
    pub percent: u8,
    pub error: String,
}

pub struct Engine {
    state: Mutex<State>,
    pub logs: Logs,
    link: Link,
    apps: Mutex<Apps>,
    inbox: Mutex<Sender<Cmd>>,
    inbox_rx: Mutex<Option<Receiver<Cmd>>>,
    /// The registry as last read by Add app.
    pub registry: Mutex<Vec<Manifest>>,
    pub mirror: Mutex<Mirror>,
    fw_dir: Option<std::path::PathBuf>,
    update_started: Mutex<Option<Instant>>,
}

impl Engine {
    pub fn start(app: AppHandle, logs: Logs) -> Arc<Engine> {
        let fw_dir = crate::flash::dir(app.path().resource_dir().ok());
        let bundled_firmware = fw_dir.as_deref().and_then(crate::flash::bundled_version);
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
                bundled_firmware,
                update: None,
            }),
            logs,
            link,
            apps: Mutex::new(apps),
            inbox: Mutex::new(tx),
            inbox_rx: Mutex::new(Some(rx)),
            registry: Mutex::new(Vec::new()),
            mirror: Mutex::new(Mirror {
                canvas: StreamCanvas::default(),
                version: 0,
                on: false,
                last_poll: Instant::now(),
            }),
            fw_dir,
            update_started: Mutex::new(None),
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
        self.state.lock().unwrap().clone()
    }

    pub fn send(&self, m: Msg) {
        self.link.send(m);
    }

    /// The Board page asks for the mirror; this starts the stream on first use.
    pub fn mirror_poll(&self) {
        let mut m = self.mirror.lock().unwrap();
        m.last_poll = Instant::now();
        if !m.on && self.state.lock().unwrap().status == Status::Connected {
            m.on = true;
            self.link.send(Msg::StreamStart { fps: MIRROR_FPS });
            self.link.send(Msg::StreamSync);
        }
    }

    pub fn mirror_stop(&self) {
        let mut m = self.mirror.lock().unwrap();
        if m.on {
            m.on = false;
            self.link.send(Msg::StreamStop);
        }
    }

    fn set_update(&self, app: &AppHandle, stage: &str, percent: u8, error: &str) {
        self.state.lock().unwrap().update = Some(Update {
            stage: stage.into(),
            percent,
            error: error.into(),
        });
        self.emit_state(app);
    }

    /// Forget a finished or failed update (the dialog was closed).
    pub fn clear_update(&self, app: &AppHandle) {
        let mut s = self.state.lock().unwrap();
        if s.update
            .as_ref()
            .is_some_and(|u| u.stage == "done" || u.stage == "failed")
        {
            s.update = None;
        }
        drop(s);
        self.emit_state(app);
    }

    /// Flash the bundled firmware to the board (update dialog, Reinstall firmware). Runs on its
    /// own thread; progress arrives in `State::update`.
    pub fn start_update(self: &Arc<Self>, app: &AppHandle) -> Result<(), String> {
        let port = {
            let s = self.state.lock().unwrap();
            if s.update
                .as_ref()
                .is_some_and(|u| u.stage == "writing" || u.stage == "restarting")
            {
                return Err("an update is already running".into());
            }
            s.board
                .as_ref()
                .map(|b| b.port.clone())
                .or_else(|| s.silent.first().map(|x| x.port.clone()))
                .ok_or("no board found")?
        };
        let dir = self
            .fw_dir
            .clone()
            .ok_or("this app has no bundled firmware")?;
        let img = crate::flash::load(&dir)?;
        self.set_update(app, "writing", 0, "");
        *self.update_started.lock().unwrap() = Some(Instant::now());
        let (me, app) = (self.clone(), app.clone());
        thread::Builder::new()
            .name("fenix-flash".into())
            .spawn(move || {
                me.log(
                    &app,
                    Source::Pc,
                    false,
                    format!("Updating the board to fenixOS v{} on {port}", img.version),
                );
                me.link.pause(true);
                thread::sleep(Duration::from_millis(500)); // the link thread closes the port
                let mut last = 0u8;
                let r = crate::flash::flash(&port, &img, &mut |f| {
                    let pct = (f * 100.0).clamp(0.0, 100.0) as u8;
                    if pct != last {
                        last = pct;
                        me.set_update(&app, "writing", pct, "");
                    }
                });
                match r {
                    Ok(()) => {
                        me.log(
                            &app,
                            Source::Pc,
                            false,
                            "Firmware written; waiting for the board to restart",
                        );
                        me.set_update(&app, "restarting", 100, "");
                    }
                    Err(e) => {
                        me.log(
                            &app,
                            Source::Pc,
                            true,
                            format!("Firmware update failed: {e}"),
                        );
                        me.set_update(&app, "failed", last, &e);
                    }
                }
                me.link.pause(false);
            })
            .map_err(|e| e.to_string())?;
        Ok(())
    }

    fn emit_state(&self, app: &AppHandle) {
        let snap = self.snapshot();
        crate::tray::update(app, &snap);
        if window_open(app) {
            let _ = app.emit_to("main", "state", snap);
        }
    }

    /// Hand a change to the engine thread.
    pub fn post(&self, c: Cmd) {
        let _ = self.inbox.lock().unwrap().send(c);
    }

    /// Read something from the app manager (config, installed versions).
    pub fn with_apps<T>(&self, f: impl FnOnce(&mut Apps) -> T) -> T {
        f(&mut self.apps.lock().unwrap())
    }

    /// Settings > Connection port: `None` finds the board on any CH340 port.
    pub fn set_port(&self, app: &AppHandle, port: Option<String>) -> Result<(), String> {
        self.with_apps(|a| {
            a.cfg.port = port.clone();
            a.cfg.save()
        })
        .map_err(|e| e.to_string())?;
        self.link.set_port(port.clone());
        let text = match &port {
            Some(p) => format!("Connection port set to {p}"),
            None => "Connection port set to Automatic".into(),
        };
        self.log(app, Source::Pc, false, text);
        Ok(())
    }

    pub fn port_setting(&self) -> Option<String> {
        self.with_apps(|a| a.cfg.port.clone())
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
            let idle = {
                let m = self.mirror.lock().unwrap();
                m.on && m.last_poll.elapsed() > MIRROR_IDLE
            };
            if idle {
                self.mirror_stop();
            }
            self.flush_app_logs(&app);
            if self.state.lock().unwrap().status == Status::Connected && Instant::now() >= next_sync
            {
                next_sync = Instant::now() + TIME_SYNC_EVERY;
                self.sync_time(&app);
            }
            if changed {
                let views = self.apps.lock().unwrap().views();
                self.state.lock().unwrap().apps = views;
                self.emit_state(&app);
            }
        }
    }

    fn flush_app_logs(&self, app: &AppHandle) {
        let lines = std::mem::take(&mut self.apps.lock().unwrap().logs);
        for (source, problem, text) in lines {
            self.log(app, source, problem, text);
        }
    }

    /// The board came back after a flash: check it now runs the bundled version.
    fn finish_update(&self, app: &AppHandle, fw: &str) {
        let mut s = self.state.lock().unwrap();
        if !s.update.as_ref().is_some_and(|u| u.stage == "restarting") {
            return;
        }
        let secs = self
            .update_started
            .lock()
            .unwrap()
            .map_or(0.0, |t| t.elapsed().as_secs_f32());
        let ok = s.bundled_firmware.as_deref() == Some(fw);
        let error = if ok {
            String::new()
        } else {
            format!("the board still reports v{fw}")
        };
        s.update = Some(Update {
            stage: if ok { "done" } else { "failed" }.into(),
            percent: 100,
            error: error.clone(),
        });
        drop(s);
        if ok {
            self.log(
                app,
                Source::Pc,
                false,
                format!("Board updated to fenixOS v{fw} in {secs:.0} s"),
            );
        } else {
            self.log(
                app,
                Source::Pc,
                true,
                format!("Firmware update failed: {error}"),
            );
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
                let now_ms = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .map_or(0, |d| d.as_millis() as u64);
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
                        booted_at_ms: now_ms.saturating_sub(info.uptime_s as u64 * 1000),
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
                self.finish_update(app, &fw);
                self.apps.lock().unwrap().board_connected(firmware_ok);
                self.mirror.lock().unwrap().on = false;
                true
            }
            Event::Disconnected { reason } => {
                self.apps.lock().unwrap().board_disconnected();
                self.mirror.lock().unwrap().on = false;
                let was = {
                    let mut s = self.state.lock().unwrap();
                    let was = s.status == Status::Connected;
                    s.status = Status::Searching;
                    s.firmware_ok = false;
                    s.board = None;
                    was
                };
                if was {
                    // A restart is not a problem by itself: the next boot line says why it restarted.
                    // "paused": the link let go of the port for a firmware update.
                    // "port changed": Settings picked another connection port.
                    let problem = !matches!(
                        reason.as_str(),
                        "board restarted" | "paused" | "port changed"
                    );
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
            Event::Msg(m @ (Msg::StreamInfo { .. } | Msg::StreamRect(_))) => {
                let mut mi = self.mirror.lock().unwrap();
                if mi.canvas.apply(&m).is_some() {
                    mi.version += 1;
                }
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
