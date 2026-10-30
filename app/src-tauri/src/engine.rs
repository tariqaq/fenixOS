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

/// Live copy of the board screen for the Board page (artboard 11).
pub struct Mirror {
    pub canvas: StreamCanvas,
    /// Bumped on every completed rectangle; the UI asks "anything newer than N?".
    pub version: u64,
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
    /// The board's radio (RADIO_STATE, firmware 0.1.8 and later; None before).
    pub radio: Option<fenix_proto::RadioState>,
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
    /// The board streams its screen (P14): started or stopped on the Board page or on the
    /// board's Stream app, which reports every change.
    pub streaming: bool,
}

/// Settings > Wireless (P34): networks heard by the board (Wi-Fi mode) or, otherwise, this PC.
#[derive(Debug, Clone, Default, Serialize)]
pub struct WifiScanView {
    pub scanning: bool,
    /// The list came from Windows (`netsh`), not the board.
    pub from_pc: bool,
    pub aps: Vec<fenix_proto::WifiAp>,
    pub error: String,
}

/// The Task manager's view (P27): the board's latest SYSINFO answer.
#[derive(Debug, Clone, Default, Serialize)]
pub struct SysinfoView {
    pub sample: Option<fenix_proto::Sysinfo>,
    /// The board answered SYSINFO_REQ with ERROR unknown type (firmware before v0.1.5).
    pub unsupported: bool,
    #[serde(skip)]
    asked: Option<Instant>,
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Update {
    /// `erasing` (first install only), `writing`, `restarting`, `done` or `failed`.
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
    sysinfo: Mutex<SysinfoView>,
    wifi: Arc<Mutex<WifiScanView>>,
    fw_dir: Option<std::path::PathBuf>,
    update_started: Mutex<Option<Instant>>,
}

impl Engine {
    /// Release the board's serial port before the app exits (see `Link::shutdown`).
    pub fn shutdown(&self) {
        self.link.shutdown();
    }

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
                streaming: false,
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
            }),
            sysinfo: Mutex::new(SysinfoView::default()),
            wifi: Arc::new(Mutex::new(WifiScanView::default())),
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

    /// Start or stop the board's stream from the PC (the Board page only stops it: the board's
    /// Stream app starts it, P15). The board reports the change back (P14); the state flips now
    /// so the page does not wait for it (and for v0.1.0 boards, which do not report).
    /// Ask the board for a new SYSINFO (the Task manager calls this every second) and return the
    /// latest one. Nothing is asked while no board is connected or the board does not know it.
    pub fn sysinfo(&self) -> SysinfoView {
        let connected = self.state.lock().unwrap().status == Status::Connected;
        let mut si = self.sysinfo.lock().unwrap();
        if connected && !si.unsupported {
            si.asked = Some(Instant::now());
            self.link.send(Msg::SysinfoReq);
        }
        si.clone()
    }

    /// Switch the board's radio; it saves the mode and restarts (P32).
    pub fn radio_set(&self, app: &AppHandle, mode: u8) {
        let name = ["off", "Bluetooth", "Wi-Fi"].get(mode as usize).copied().unwrap_or("?");
        self.log(app, Source::Pc, false, format!("Asked the board to switch its radio to {name} (it restarts)"));
        self.link.send(Msg::RadioSet(mode));
    }

    /// Start a Wi-Fi scan: on the board in Wi-Fi mode, else on this PC (2.4 GHz only).
    pub fn wifi_scan_start(&self) {
        let board_wifi = {
            let s = self.state.lock().unwrap();
            s.status == Status::Connected
                && s.board.as_ref().and_then(|b| b.radio.as_ref()).is_some_and(|r| r.mode == fenix_proto::radio::WIFI)
        };
        let mut w = self.wifi.lock().unwrap();
        if w.scanning {
            return;
        }
        w.scanning = true;
        w.error.clear();
        if board_wifi {
            self.link.send(Msg::WifiScanReq);
            return;
        }
        let wifi = self.wifi.clone();
        std::thread::spawn(move || {
            let r = crate::wifi::scan();
            let mut w = wifi.lock().unwrap();
            w.scanning = false;
            w.from_pc = true;
            match r {
                Ok(aps) => w.aps = aps,
                Err(e) => w.error = e,
            }
        });
    }

    pub fn wifi_scan(&self) -> WifiScanView {
        self.wifi.lock().unwrap().clone()
    }

    /// Save a network on the board (plaintext, user choice) and connect; empty SSID forgets.
    pub fn wifi_join(&self, app: &AppHandle, ssid: String, password: String) {
        let what = if ssid.is_empty() { "forget its network".to_string() } else { format!("join \"{ssid}\"") };
        self.log(app, Source::Pc, false, format!("Asked the board to {what}"));
        self.link.send(Msg::WifiJoin { ssid, password });
    }

    pub fn mirror_set(&self, app: &AppHandle, on: bool) {
        if self.state.lock().unwrap().status != Status::Connected {
            return;
        }
        if on {
            self.link.send(Msg::StreamStart { fps: MIRROR_FPS });
            self.link.send(Msg::StreamSync);
        } else {
            self.link.send(Msg::StreamStop);
        }
        self.set_streaming(app, on);
    }

    fn set_streaming(&self, app: &AppHandle, on: bool) {
        let changed = {
            let mut s = self.state.lock().unwrap();
            std::mem::replace(&mut s.streaming, on) != on
        };
        if changed {
            self.emit_state(app);
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

    /// The port to flash or probe: the connected board, else the first CH340 port that did not
    /// answer as fenixOS.
    fn target_port(&self) -> Result<String, String> {
        let s = self.state.lock().unwrap();
        s.board
            .as_ref()
            .map(|b| b.port.clone())
            .or_else(|| s.silent.first().map(|x| x.port.clone()))
            .ok_or_else(|| "no board found".into())
    }

    /// First run: ask the bootloader of a board that does not answer as fenixOS what it is.
    /// Blocks for a few seconds (call it from an async command).
    pub fn probe(&self, app: &AppHandle) -> Result<crate::flash::Probe, String> {
        let port = self.target_port()?;
        self.log(
            app,
            Source::Pc,
            false,
            format!("Checking the board on {port}"),
        );
        self.link.pause(true);
        thread::sleep(Duration::from_millis(500)); // the link thread closes the port
        let r = crate::flash::probe(&port);
        self.link.pause(false);
        match &r {
            Ok(p) => self.log(
                app,
                Source::Pc,
                false,
                format!("{port}: {}, {} flash, MAC {}", p.chip, p.flash, p.mac),
            ),
            Err(e) => self.log(app, Source::Pc, true, format!("Board check failed: {e}")),
        }
        r
    }

    /// Flash the bundled firmware to the board (update dialog, Reinstall firmware; `erase` for the
    /// first install). Runs on its own thread; progress arrives in `State::update`.
    pub fn start_update(self: &Arc<Self>, app: &AppHandle, erase: bool) -> Result<(), String> {
        let port = {
            let s = self.state.lock().unwrap();
            if s.update
                .as_ref()
                .is_some_and(|u| u.stage != "done" && u.stage != "failed")
            {
                return Err("an update is already running".into());
            }
            drop(s);
            self.target_port()?
        };
        let dir = self
            .fw_dir
            .clone()
            .ok_or("this app has no bundled firmware")?;
        let img = crate::flash::load(&dir)?;
        self.set_update(app, if erase { "erasing" } else { "writing" }, 0, "");
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
                let r = crate::flash::flash(&port, &img, erase, &mut |f| {
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
        emit_ui(app, "state", snap);
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
        emit_ui(app, "log", line);
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
                        radio: None,
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
                *self.sysinfo.lock().unwrap() = SysinfoView::default();
                self.sync_time(app);
                self.finish_update(app, &fw);
                self.apps.lock().unwrap().board_connected(firmware_ok);
                // Ask whether the board streams: it answers STREAM_START or STREAM_STOP (P15). It
                // stops streaming after 3 s without the link, so this matters for quick reconnects.
                self.state.lock().unwrap().streaming = false;
                self.link.send(Msg::StreamSync);
                true
            }
            Event::Disconnected { reason } => {
                self.apps.lock().unwrap().board_disconnected();
                self.wifi.lock().unwrap().scanning = false;
                self.state.lock().unwrap().streaming = false;
                *self.sysinfo.lock().unwrap() = SysinfoView::default();
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
            Event::Msg(Msg::Sysinfo(s)) => {
                self.sysinfo.lock().unwrap().sample = Some(s);
                false
            }
            Event::Msg(Msg::RadioState(r)) => {
                let mut s = self.state.lock().unwrap();
                match s.board.as_mut() {
                    Some(b) if b.radio.as_ref() != Some(&r) => {
                        b.radio = Some(r);
                        true
                    }
                    _ => false,
                }
            }
            Event::Msg(Msg::WifiScan { status, aps }) => {
                let mut w = self.wifi.lock().unwrap();
                w.scanning = false;
                w.from_pc = false;
                w.aps = aps;
                w.error = match status {
                    fenix_proto::scan_status::OK => String::new(),
                    fenix_proto::scan_status::NOT_WIFI => "The board is not in Wi-Fi mode".into(),
                    _ => "The board could not scan".into(),
                };
                false
            }
            Event::Msg(Msg::Error { code, text }) => {
                // Firmware before v0.1.5 does not know SYSINFO_REQ: stop asking, once.
                {
                    let mut si = self.sysinfo.lock().unwrap();
                    let just_asked = si
                        .asked
                        .is_some_and(|t| t.elapsed() < Duration::from_secs(2));
                    if code == fenix_proto::err::UNKNOWN_TYPE && just_asked && si.sample.is_none() {
                        si.unsupported = true;
                        return false;
                    }
                }
                self.log(app, Source::Board, true, format!("Error {code}: {text}"));
                false
            }
            // The board's own stream reports (its Stream app, or the answer to ours).
            Event::Msg(Msg::StreamStart { .. }) => {
                self.set_streaming(app, true);
                false
            }
            Event::Msg(Msg::StreamStop) => {
                self.set_streaming(app, false);
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

/// Send an event to every open fenixOS window (main and the first-run welcome).
pub fn emit_ui<S: Serialize + Clone>(app: &AppHandle, event: &str, payload: S) {
    for label in ["main", "welcome"] {
        if app.get_webview_window(label).is_some() {
            let _ = app.emit_to(label, event, payload.clone());
        }
    }
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
