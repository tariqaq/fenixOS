//! App manager (PLAN §3.5): installed apps, their switches and settings, the board install queue,
//! and the PC halves. Runs on the engine thread; the UI changes it through `Cmd`s.
//! Files: `%LOCALAPPDATA%\fenixOS\apps\<id>\<version>\{manifest.json, app.fxp, pc.exe}`.

use std::collections::{BTreeMap, VecDeque};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::mpsc::Sender;
use std::time::{Duration, Instant};

use fenix_proto::{app as code, BindValue, Msg, MAX_CHUNK};
use fenix_sdk::wire::{Down, Up};
use serde::Serialize;
use serde_json::{Map, Value};
use sha2::{Digest, Sha256};

use crate::config::{AppCfg, Config};
use crate::logs::Source;
use crate::registry::{self, Manifest};
use crate::sidecar::{Note, Sidecar, Spec};

const WINDOW: usize = 4 * MAX_CHUNK;
const OP_TIMEOUT: Duration = Duration::from_secs(30);
const RESTART_AFTER: [u64; 3] = [1, 5, 15];
const FAIL_WINDOW: Duration = Duration::from_secs(120);

/// A change asked for by the UI or reported by a worker thread.
pub enum Cmd {
    SetEnabled {
        id: String,
        on: bool,
    },
    SetSetting {
        id: String,
        key: String,
        value: Value,
    },
    Button {
        id: String,
        key: String,
    },
    Remove {
        id: String,
    },
    /// Files of a new or updated app are in place (see `fetch_app`).
    Added {
        manifest: Box<Manifest>,
        enable: bool,
    },
    Sidecar {
        id: String,
        gen: u64,
        note: Note,
    },
}

/// One app as the UI shows it (tiles, App settings).
#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct AppView {
    pub id: String,
    pub name: String,
    pub description: String,
    pub version: String,
    pub color: String,
    pub icon: String,
    pub repo: String,
    pub enabled: bool,
    /// `off`, `on`, `installing`, `waiting` (no board), `update` (board firmware differs), `error`.
    pub status: String,
    pub detail: String,
    pub permissions: Vec<String>,
    pub has_pc: bool,
    pub elevated: bool,
    pub settings: Vec<Value>,
    pub values: Map<String, Value>,
}

struct Installed {
    manifest: Manifest,
    pkg: fenix_pkg::Package,
    fxp: Vec<u8>,
    sha: [u8; 32],
    exe: Option<PathBuf>,
}

#[derive(Default)]
struct Run {
    sidecar: Option<Sidecar>,
    gen: u64,
    ready: bool,
    fails: Vec<Instant>,
    restart_at: Option<Instant>,
    error: Option<String>,
}

enum Op {
    Install(String),
    Remove(String),
}

struct Current {
    op: Op,
    sent: usize,
    acked: usize,
    ended: bool,
    last_ack: Instant,
    started: Instant,
}

pub struct Apps {
    pub cfg: Config,
    root: PathBuf,
    installed: BTreeMap<String, Installed>,
    run: BTreeMap<String, Run>,
    gen: u64,
    link: Sender<Msg>,
    inbox: Sender<Cmd>,
    /// Board link up, firmware matches and APP_LIST received: installs may go ahead.
    board_ready: bool,
    connected: bool,
    firmware_ok: bool,
    on_board: BTreeMap<String, [u8; 32]>,
    ops: VecDeque<Op>,
    current: Option<Current>,
    open_app: Option<String>,
    /// Lines for the log, flushed by the engine (it owns the AppHandle).
    pub logs: Vec<(Source, bool, String)>,
}

pub fn root() -> PathBuf {
    std::env::var_os("LOCALAPPDATA")
        .map(PathBuf::from)
        .unwrap_or_else(std::env::temp_dir)
        .join("fenixOS")
        .join("apps")
}

fn app_dir(root: &Path, id: &str, version: &str) -> PathBuf {
    root.join(id).join(version)
}

fn load(root: &Path, id: &str, version: &str) -> Result<Installed, String> {
    let dir = app_dir(root, id, version);
    let manifest: Manifest = fs::read(dir.join("manifest.json"))
        .map_err(|e| e.to_string())
        .and_then(|b| serde_json::from_slice(&b).map_err(|e| e.to_string()))?;
    let fxp = fs::read(dir.join("app.fxp")).map_err(|e| e.to_string())?;
    let pkg = fenix_pkg::parse(&fxp)?;
    let exe = dir.join("pc.exe");
    Ok(Installed {
        sha: Sha256::digest(&fxp).into(),
        exe: manifest.pc.as_ref().map(|_| exe),
        manifest,
        pkg,
        fxp,
    })
}

/// Download an app's files from the registry into its folder (worker thread, not the engine's).
/// Checks sizes, checksums, and that the package matches the manifest.
pub fn fetch_app(src: &registry::Source, m: &Manifest) -> Result<(), String> {
    if !registry::version_at_least(env!("CARGO_PKG_VERSION"), &m.min_fenixos) {
        return Err(format!("needs fenixOS {} or newer", m.min_fenixos));
    }
    let fxp = registry::download(src, &m.board)?;
    let pkg = fenix_pkg::parse(&fxp)?;
    if pkg.id() != m.id || pkg.version() != m.version {
        return Err(format!(
            "package is {} v{}, manifest says {} v{}",
            pkg.id(),
            pkg.version(),
            m.id,
            m.version
        ));
    }
    let exe = match &m.pc {
        Some(pc) => Some(registry::download(src, pc)?),
        None => None,
    };
    let dir = app_dir(&root(), &m.id, &m.version);
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let write = |name: &str, data: &[u8]| {
        fs::write(dir.join(name), data).map_err(|e| format!("{name}: {e}"))
    };
    write("app.fxp", &fxp)?;
    if let Some(exe) = exe {
        write("pc.exe", &exe)?;
    }
    write(
        "manifest.json",
        &serde_json::to_vec_pretty(m).map_err(|e| e.to_string())?,
    )
}

impl Apps {
    pub fn new(link: Sender<Msg>, inbox: Sender<Cmd>) -> Apps {
        let mut a = Apps {
            cfg: Config::load(),
            root: root(),
            installed: BTreeMap::new(),
            run: BTreeMap::new(),
            gen: 0,
            link,
            inbox,
            board_ready: false,
            connected: false,
            firmware_ok: false,
            on_board: BTreeMap::new(),
            ops: VecDeque::new(),
            current: None,
            open_app: None,
            logs: Vec::new(),
        };
        let ids: Vec<(String, String)> = a
            .cfg
            .apps
            .iter()
            .map(|(k, v)| (k.clone(), v.version.clone()))
            .collect();
        for (id, version) in ids {
            match load(&a.root, &id, &version) {
                Ok(i) => {
                    a.installed.insert(id, i);
                }
                Err(e) => {
                    a.log(
                        true,
                        format!("{id} v{version} could not be loaded ({e}); removed from the list"),
                    );
                    a.cfg.apps.remove(&id);
                }
            }
        }
        a.save();
        let enabled: Vec<String> = a
            .cfg
            .apps
            .iter()
            .filter(|(_, c)| c.enabled)
            .map(|(k, _)| k.clone())
            .collect();
        for id in enabled {
            a.start_sidecar(&id);
        }
        a
    }

    fn log(&mut self, problem: bool, text: impl Into<String>) {
        self.logs.push((Source::Apps, problem, text.into()));
    }

    fn save(&mut self) {
        if let Err(e) = self.cfg.save() {
            self.logs
                .push((Source::Pc, true, format!("Could not save settings: {e}")));
        }
    }

    fn name(&self, id: &str) -> String {
        self.installed
            .get(id)
            .map(|i| i.manifest.name.clone())
            .unwrap_or_else(|| id.into())
    }

    pub fn is_installed(&self, id: &str, version: &str) -> bool {
        self.cfg.apps.get(id).is_some_and(|c| c.version == version)
    }

    // ---- UI ----

    pub fn views(&self) -> Vec<AppView> {
        self.installed
            .iter()
            .filter_map(|(id, i)| {
                let c = self.cfg.apps.get(id)?;
                let r = self.run.get(id);
                let m = &i.manifest;
                let busy = matches!(&self.current, Some(Current { op: Op::Install(x), .. }) if x == id)
                    || self.ops.iter().any(|o| matches!(o, Op::Install(x) if x == id));
                let (status, detail) = if !c.enabled {
                    ("off", "Off".to_string())
                } else if let Some(e) = r.and_then(|r| r.error.clone()) {
                    ("error", e)
                } else if !self.connected {
                    ("waiting", "On, waiting for the board".into())
                } else if !self.firmware_ok {
                    ("update", "On, update the board first".into())
                } else if busy || !self.board_ready {
                    ("installing", "Installing on the board".into())
                } else {
                    ("on", "On, installed on the board".into())
                };
                Some(AppView {
                    id: id.clone(),
                    name: m.name.clone(),
                    description: m.description.clone(),
                    version: m.version.clone(),
                    color: m.color.clone(),
                    icon: m.icon.clone(),
                    repo: m.repo.clone(),
                    enabled: c.enabled,
                    status: status.into(),
                    detail,
                    permissions: m.permissions.clone(),
                    has_pc: m.pc.is_some(),
                    elevated: m.pc.as_ref().is_some_and(|p| p.elevated),
                    settings: m.settings.clone(),
                    values: c.settings.clone(),
                })
            })
            .collect()
    }

    /// Apply one command. True when what the UI shows changed.
    pub fn command(&mut self, cmd: Cmd) -> bool {
        match cmd {
            Cmd::SetEnabled { id, on } => self.set_enabled(&id, on),
            Cmd::SetSetting { id, key, value } => {
                let Some(c) = self.cfg.apps.get_mut(&id) else {
                    return false;
                };
                c.settings.insert(key, value);
                let values = c.settings.clone();
                self.save();
                self.to_sidecar(&id, Down::Settings { values });
                true
            }
            Cmd::Button { id, key } => {
                self.to_sidecar(&id, Down::Button { key });
                false
            }
            Cmd::Remove { id } => {
                self.set_enabled(&id, false);
                let name = self.name(&id);
                self.installed.remove(&id);
                self.cfg.apps.remove(&id);
                self.run.remove(&id);
                self.save();
                let _ = fs::remove_dir_all(self.root.join(&id));
                self.log(false, format!("{name} removed"));
                true
            }
            Cmd::Added { manifest, enable } => self.added(*manifest, enable),
            Cmd::Sidecar { id, gen, note } => self.sidecar_note(&id, gen, note),
        }
    }

    fn added(&mut self, m: Manifest, enable: bool) -> bool {
        let id = m.id.clone();
        let old = self.cfg.apps.get(&id).cloned();
        let was_on = old.as_ref().is_some_and(|c| c.enabled);
        if was_on {
            self.stop_sidecar(&id);
        }
        let installed = match load(&self.root, &id, &m.version) {
            Ok(i) => i,
            Err(e) => {
                self.log(true, format!("{} could not be added: {e}", m.name));
                return false;
            }
        };
        // Keep the user's values for settings that still exist; new ones start at their default.
        let mut settings = registry::default_settings(&m);
        if let Some(o) = &old {
            for (k, v) in &o.settings {
                if settings.contains_key(k) {
                    settings.insert(k.clone(), v.clone());
                }
            }
            if o.version != m.version {
                let _ = fs::remove_dir_all(app_dir(&self.root, &id, &o.version));
            }
        }
        self.installed.insert(id.clone(), installed);
        self.cfg.apps.insert(
            id.clone(),
            AppCfg {
                version: m.version.clone(),
                enabled: false,
                settings,
            },
        );
        self.save();
        self.log(false, format!("{} v{} added", m.name, m.version));
        self.set_enabled(&id, enable || was_on);
        true
    }

    fn set_enabled(&mut self, id: &str, on: bool) -> bool {
        let Some(c) = self.cfg.apps.get_mut(id) else {
            return false;
        };
        let changed = c.enabled != on;
        c.enabled = on;
        self.save();
        let name = self.name(id);
        if on {
            if let Some(r) = self.run.get_mut(id) {
                r.fails.clear();
                r.error = None;
            }
            if changed {
                self.log(false, format!("{name} switched on"));
            }
            if self.run.get(id).is_none_or(|r| r.sidecar.is_none()) {
                self.start_sidecar(id);
            }
            self.queue_install(id);
        } else {
            if changed {
                self.log(false, format!("{name} switched off"));
            }
            self.stop_sidecar(id);
            if self.board_ready {
                let _ = self.link.send(Msg::HostState {
                    running: false,
                    id: id.into(),
                });
            }
            self.ops.retain(|o| !matches!(o, Op::Install(x) if x == id));
            if self.on_board.contains_key(id) {
                self.ops.push_back(Op::Remove(id.into()));
            }
        }
        true
    }

    fn queue_install(&mut self, id: &str) {
        let Some(i) = self.installed.get(id) else {
            return;
        };
        if self.on_board.get(id) == Some(&i.sha) {
            self.send_host_state(id);
            return;
        }
        let queued = self
            .ops
            .iter()
            .any(|o| matches!(o, Op::Install(x) if x == id))
            || matches!(&self.current, Some(Current { op: Op::Install(x), .. }) if x == id);
        if !queued {
            self.ops.retain(|o| !matches!(o, Op::Remove(x) if x == id));
            self.ops.push_back(Op::Install(id.into()));
        }
    }

    // ---- Board ----

    pub fn board_connected(&mut self, firmware_ok: bool) {
        self.connected = true;
        self.firmware_ok = firmware_ok;
        self.board_ready = false;
        self.on_board.clear();
        self.ops.clear();
        self.current = None;
        self.open_app = None;
        if firmware_ok {
            let _ = self.link.send(Msg::AppListReq);
        }
        self.all_sidecars(Down::Board { connected: true });
    }

    pub fn board_disconnected(&mut self) {
        self.connected = false;
        self.board_ready = false;
        self.ops.clear();
        self.current = None;
        if let Some(id) = self.open_app.take() {
            self.to_sidecar(&id, Down::Closed);
        }
        self.all_sidecars(Down::Board { connected: false });
    }

    /// A message from the board. True when what the UI shows changed.
    pub fn board_msg(&mut self, m: &Msg) -> bool {
        match m {
            Msg::AppList(list) if self.connected && self.firmware_ok => {
                self.on_board = list.iter().map(|e| (e.id.clone(), e.sha256)).collect();
                self.board_ready = true;
                self.reconcile();
                true
            }
            Msg::AppAck { next_offset } => {
                if let Some(c) = self.current.as_mut() {
                    c.acked = *next_offset as usize;
                    c.last_ack = Instant::now();
                }
                false
            }
            Msg::AppResult {
                op,
                code: rc,
                id,
                text,
            } => self.op_result(*op, *rc, id, text),
            Msg::AppOpened { id } => {
                self.open_app = Some(id.clone());
                self.to_sidecar(id, Down::Opened);
                false
            }
            Msg::AppClosed { id } => {
                if self.open_app.as_deref() == Some(id) {
                    self.open_app = None;
                }
                self.to_sidecar(id, Down::Closed);
                false
            }
            Msg::Event {
                elem,
                kind,
                value,
                id,
            } => {
                let name = self
                    .installed
                    .get(id)
                    .and_then(|i| i.pkg.event_name(*elem))
                    .map(String::from);
                if let Some(name) = name {
                    let kind = match kind {
                        2 => "long",
                        3 => "change",
                        _ => "tap",
                    };
                    self.to_sidecar(
                        id,
                        Down::Event {
                            name,
                            kind: kind.into(),
                            value: *value,
                        },
                    );
                }
                false
            }
            _ => false,
        }
    }

    /// The PC is the source of truth: switched-on apps go on the board, everything else comes off.
    fn reconcile(&mut self) {
        let extra: Vec<String> = self
            .on_board
            .keys()
            .filter(|id| !self.cfg.apps.get(*id).is_some_and(|c| c.enabled))
            .cloned()
            .collect();
        for id in extra {
            self.log(
                false,
                format!("Removing {id} from the board (not switched on here)"),
            );
            self.ops.push_back(Op::Remove(id));
        }
        let on: Vec<String> = self
            .cfg
            .apps
            .iter()
            .filter(|(_, c)| c.enabled)
            .map(|(k, _)| k.clone())
            .collect();
        for id in on {
            self.queue_install(&id);
        }
    }

    fn send_host_state(&mut self, id: &str) {
        if !self.board_ready {
            return;
        }
        let Some(i) = self.installed.get(id) else {
            return;
        };
        let running = i.manifest.pc.is_none() || self.run.get(id).is_some_and(|r| r.ready);
        let _ = self.link.send(Msg::HostState {
            running,
            id: id.into(),
        });
    }

    /// Drive the board queue: one install or remove at a time. True when the UI changed.
    pub fn tick(&mut self) -> bool {
        let mut changed = self.restart_due();
        if !self.board_ready {
            return changed;
        }
        if self.current.is_none() {
            let Some(op) = self.ops.pop_front() else {
                return changed;
            };
            match &op {
                Op::Install(id) => {
                    let Some(i) = self.installed.get(id) else {
                        return changed;
                    };
                    let _ = self.link.send(Msg::AppInstallBegin {
                        id: id.clone(),
                        version: i.manifest.version.clone(),
                        size: i.fxp.len() as u32,
                        sha256: i.sha,
                    });
                }
                Op::Remove(id) => {
                    let _ = self.link.send(Msg::AppRemove { id: id.clone() });
                }
            }
            self.current = Some(Current {
                op,
                sent: 0,
                acked: 0,
                ended: false,
                last_ack: Instant::now(),
                started: Instant::now(),
            });
            changed = true;
        }
        let Some(c) = self.current.as_mut() else {
            return changed;
        };
        if c.started.elapsed() > OP_TIMEOUT {
            let id = match &c.op {
                Op::Install(x) | Op::Remove(x) => x.clone(),
            };
            self.current = None;
            self.log(true, format!("{id}: the board did not answer in 30 s"));
            return true;
        }
        if let Op::Install(id) = &c.op {
            let Some(i) = self.installed.get(id) else {
                self.current = None;
                return true;
            };
            let data = &i.fxp;
            if c.last_ack.elapsed() > Duration::from_secs(1) && c.sent > c.acked {
                c.sent = c.acked; // lost chunks: rewind to the last ack
                c.last_ack = Instant::now();
            }
            while c.sent < data.len() && c.sent < c.acked + WINDOW {
                let n = (data.len() - c.sent).min(MAX_CHUNK);
                let _ = self.link.send(Msg::AppChunk {
                    offset: c.sent as u32,
                    data: data[c.sent..c.sent + n].to_vec(),
                });
                c.sent += n;
            }
            if c.acked == data.len() && !c.ended {
                let _ = self.link.send(Msg::AppInstallEnd);
                c.ended = true;
            }
        }
        changed
    }

    /// True while an install or remove is running (the engine polls faster then).
    pub fn busy(&self) -> bool {
        self.current.is_some() || (self.board_ready && !self.ops.is_empty())
    }

    fn op_result(&mut self, op: u8, rc: u8, id: &str, text: &str) -> bool {
        let Some(c) = self.current.take() else {
            return false;
        };
        let secs = c.started.elapsed().as_secs_f32();
        let name = self.name(id);
        match (op, rc) {
            (code::OP_INSTALL, code::OK) => {
                if let Some(i) = self.installed.get(id) {
                    self.on_board.insert(id.into(), i.sha);
                }
                self.log(
                    false,
                    format!("{name} installed on the board ({secs:.1} s)"),
                );
                self.send_host_state(id);
            }
            (code::OP_REMOVE, code::OK | code::UNKNOWN_APP) => {
                self.on_board.remove(id);
                self.log(false, format!("{name} removed from the board"));
            }
            (code::OP_INSTALL, _) => {
                let why = match rc {
                    code::NO_SPACE => "the board is full".to_string(),
                    _ => format!("code {rc}: {text}"),
                };
                self.log(
                    true,
                    format!("{name} could not be installed on the board: {why}"),
                );
                if let Some(r) = self.run.get_mut(id) {
                    r.error = Some("Could not install, see Logs".into());
                }
            }
            _ => self.log(true, format!("{name}: board answered code {rc} ({text})")),
        }
        true
    }

    // ---- PC halves ----

    fn start_sidecar(&mut self, id: &str) {
        let Some(i) = self.installed.get(id) else {
            return;
        };
        let (Some(exe), Some(pc)) = (i.exe.clone(), i.manifest.pc.clone()) else {
            return;
        };
        let Some(c) = self.cfg.apps.get(id) else {
            return;
        };
        self.gen += 1;
        let gen = self.gen;
        let spec = Spec {
            id: id.into(),
            exe,
            elevated: pc.elevated,
            hello: Down::Hello {
                app: id.into(),
                version: i.manifest.version.clone(),
                fenixos: env!("CARGO_PKG_VERSION").into(),
                protocol: fenix_sdk::wire::PROTOCOL,
            },
            settings: Down::Settings {
                values: c.settings.clone(),
            },
        };
        let inbox = self.inbox.clone();
        let owner = id.to_string();
        let sc = Sidecar::start(spec, move |note| {
            let _ = inbox.send(Cmd::Sidecar {
                id: owner.clone(),
                gen,
                note,
            });
        });
        let r = self.run.entry(id.into()).or_default();
        r.sidecar = Some(sc);
        r.gen = gen;
        r.ready = false;
        r.restart_at = None;
    }

    fn stop_sidecar(&mut self, id: &str) {
        if let Some(r) = self.run.get_mut(id) {
            if let Some(sc) = r.sidecar.take() {
                sc.stop();
            }
            r.ready = false;
            r.restart_at = None;
        }
    }

    fn to_sidecar(&self, id: &str, m: Down) {
        if let Some(sc) = self.run.get(id).and_then(|r| r.sidecar.as_ref()) {
            sc.send(m);
        }
    }

    fn all_sidecars(&self, m: Down) {
        for r in self.run.values() {
            if let Some(sc) = &r.sidecar {
                sc.send(m.clone());
            }
        }
    }

    fn sidecar_note(&mut self, id: &str, gen: u64, note: Note) -> bool {
        if self.run.get(id).is_none_or(|r| r.gen != gen) {
            return false; // from a run that was already stopped or replaced
        }
        let name = self.name(id);
        match note {
            Note::Ready => {
                if let Some(r) = self.run.get_mut(id) {
                    r.ready = true;
                }
                self.to_sidecar(
                    id,
                    Down::Board {
                        connected: self.connected,
                    },
                );
                if self.open_app.as_deref() == Some(id) {
                    self.to_sidecar(id, Down::Opened);
                }
                if self.on_board.contains_key(id) {
                    self.send_host_state(id);
                }
                false
            }
            Note::Up(Up::Set { bind, value }) => {
                if self.open_app.as_deref() != Some(id) {
                    return false; // only shown while open; the app sends again on `opened`
                }
                let Some(i) = self.installed.get(id) else {
                    return false;
                };
                let Some(b) = i.pkg.bind_id(&bind) else {
                    return false;
                };
                let value = match value {
                    Value::Bool(v) => BindValue::Bool(v),
                    Value::Number(n) => BindValue::Int(
                        n.as_i64()
                            .unwrap_or(0)
                            .clamp(i32::MIN as i64, i32::MAX as i64) as i32,
                    ),
                    Value::String(mut s) => {
                        while s.len() > 255 {
                            s.pop();
                        }
                        BindValue::Text(s)
                    }
                    _ => return false,
                };
                let _ = self.link.send(Msg::BindSet {
                    bind: b,
                    id: id.into(),
                    value,
                });
                false
            }
            Note::Up(Up::Log { level, text }) => {
                let problem = level == "warn" || level == "error";
                self.log(problem, format!("{name}: {text}"));
                false
            }
            Note::Up(Up::Unknown) => false,
            Note::Exited {
                why,
                requested,
                declined,
            } => {
                if requested {
                    return false;
                }
                let enabled = self.cfg.apps.get(id).is_some_and(|c| c.enabled);
                let Some(r) = self.run.get_mut(id) else {
                    return false;
                };
                r.sidecar = None;
                r.ready = false;
                let now = Instant::now();
                r.fails.retain(|t| now.duration_since(*t) < FAIL_WINDOW);
                r.fails.push(now);
                let n = r.fails.len();
                let msg = if declined || n > RESTART_AFTER.len() || !enabled {
                    r.error = Some("Stopped, see Logs".into());
                    if declined {
                        format!("{name}: PC part not started ({why})")
                    } else {
                        format!(
                            "{name}: PC part stopped ({why}); not restarting after {} tries",
                            n
                        )
                    }
                } else {
                    let wait = RESTART_AFTER[n - 1];
                    r.restart_at = Some(now + Duration::from_secs(wait));
                    format!("{name}: PC part stopped ({why}); restarting in {wait} s")
                };
                self.log(true, msg);
                if self.board_ready {
                    let _ = self.link.send(Msg::HostState {
                        running: false,
                        id: id.into(),
                    });
                }
                true
            }
        }
    }

    fn restart_due(&mut self) -> bool {
        let now = Instant::now();
        let due: Vec<String> = self
            .run
            .iter()
            .filter(|(_, r)| r.restart_at.is_some_and(|t| now >= t))
            .map(|(k, _)| k.clone())
            .collect();
        for id in &due {
            if self.cfg.apps.get(id).is_some_and(|c| c.enabled) {
                self.start_sidecar(id);
            } else if let Some(r) = self.run.get_mut(id) {
                r.restart_at = None;
            }
        }
        !due.is_empty()
    }
}
