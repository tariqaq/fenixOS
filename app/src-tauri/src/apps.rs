//! App manager (PLAN §3.5): installed apps, their switches and settings, the board install queue,
//! and the PC halves. Runs on the engine thread; the UI changes it through `Cmd`s.
//! Files: `%LOCALAPPDATA%\fenixOS\apps\<id>\<version>\{manifest.json, app.fxp, pc.exe}`.
//!
//! Since P51 (docs/packages.md "On, off and sync"): Add installs both halves, switching off stops
//! the PC half only (the board tile fades), Remove removes both. What differs between this PC and
//! the board when it connects is listed (`sync_items`) and changed only when the user syncs.

use std::collections::{BTreeMap, BTreeSet, VecDeque};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::mpsc::Sender;
use std::time::{Duration, Instant};

use fenix_proto::{
    app as code, app_state, app_where, host, AppIndexEntry, BindValue, Msg, MAX_CHUNK,
};
use fenix_sdk::wire::{Down, Up};
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};
use sha2::{Digest, Sha256};

use crate::config::{AppCfg, Config};
use crate::logs::Source;
use crate::registry::{self, Manifest};
use crate::sidecar::{Note, Sidecar, Spec};

const WINDOW: usize = 4 * MAX_CHUNK;
/// An install or remove gives up after this long without progress (big packages take minutes).
const OP_TIMEOUT: Duration = Duration::from_secs(30);
/// Board installs over this size show a percentage (P51).
const SHOW_PROGRESS_OVER: usize = 128 * 1024;
/// Measured over USB (P51 plan): installs that take longer than `SLOW_INSTALL` say so first.
const USB_BYTES_PER_S: usize = 42 * 1024;
const SLOW_INSTALL: Duration = Duration::from_secs(120);
/// An APP_INDEX page that does not come is asked for again.
const INDEX_RETRY: Duration = Duration::from_secs(3);
/// Binding values go out at most BIND_BURST per BIND_WINDOW (P50-M): an app sends ~50 when it is
/// opened, and the board's UI takes them between frames. Values waiting are kept latest per binding.
const BIND_BURST: u32 = 16;
const BIND_WINDOW: Duration = Duration::from_millis(50);
/// Bound-image chunks per BIND_WINDOW (P50-M): ~3 KB per 50 ms keeps the board's 5 KB UART buffer
/// safe; a 96 x 96 image (the largest, fenix_pkg::MAX_ART) takes ~0.25 s.
const IMAGE_CHUNKS: u32 = 3;
const IMAGE_CHUNK: usize = 1024;

/// A bound image on its way to the board (one at a time; a newer one replaces it).
struct ImageSend {
    id: String,
    bind: u16,
    w: u16,
    h: u16,
    data: Vec<u8>,
    offset: usize,
}
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
    /// One choice from Apps › Sync with board (`pc` is done by the caller: download, then `Added`).
    Sync {
        id: String,
        action: SyncAction,
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
    /// Setting values, without `secret` ones (P51-M): those never go to the UI.
    pub values: Map<String, Value>,
    /// Keys of the `secret` settings that hold a value.
    pub secrets_set: Vec<String>,
    /// Where the board half lives: `sd`, `flash`, `card_missing` (its SD card is out), or empty
    /// (not on the board, or no board).
    pub place: String,
}

/// The board half as the board's APP_INDEX lists it.
#[derive(Debug, Clone, PartialEq)]
pub struct BoardApp {
    pub version: String,
    /// All zero while its SD card is out.
    pub sha: [u8; 32],
    pub place: u8,
    pub state: u8,
}

impl From<AppIndexEntry> for BoardApp {
    fn from(e: AppIndexEntry) -> BoardApp {
        BoardApp {
            version: e.version,
            sha: e.sha256,
            place: e.place,
            state: e.state,
        }
    }
}

/// The PC half, for the sync diff.
#[derive(Debug, Clone, PartialEq)]
pub struct PcApp {
    pub name: String,
    pub version: String,
    pub sha: [u8; 32],
}

/// One difference between this PC and the board (Apps › Sync with board).
#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct SyncItem {
    pub id: String,
    /// The PC's name for it, or the id when this PC does not have it.
    pub name: String,
    /// `board_missing`, `board_older`, `board_differs` (sync puts this PC's version on the
    /// board), `pc_older` (sync updates the PC half from the registry), `board_only` (no PC
    /// half here: add it from the registry, keep it on the board alone, or remove it).
    pub kind: String,
    pub pc_version: String,
    pub board_version: String,
    /// The board half is on an SD card that is out (it cannot be removed now).
    pub card_missing: bool,
}

/// What the user picked for one `SyncItem`.
#[derive(Debug, Clone, Copy, Deserialize, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum SyncAction {
    /// Install this PC's version on the board.
    Board,
    /// Add or update the PC half from the registry (the caller downloads it).
    Pc,
    /// Keep a board-only app; the board shows "Not on this PC" for it.
    Keep,
    /// Remove a board-only app from the board.
    Remove,
}

/// The sync diff (P51), pure so it is tested without a board. Apps the user chose to keep on the
/// board alone (`kept`) are not listed; an app whose card is out is compared by version only.
pub fn sync_diff(
    pc: &BTreeMap<String, PcApp>,
    board: &BTreeMap<String, BoardApp>,
    kept: &BTreeSet<String>,
) -> Vec<SyncItem> {
    let mut out = Vec::new();
    let item = |id: &str, name: &str, kind: &str, pcv: &str, b: Option<&BoardApp>| SyncItem {
        id: id.into(),
        name: name.into(),
        kind: kind.into(),
        pc_version: pcv.into(),
        board_version: b.map(|b| b.version.clone()).unwrap_or_default(),
        card_missing: b.is_some_and(|b| b.state == app_state::CARD_MISSING),
    };
    for (id, p) in pc {
        let kind = match board.get(id) {
            None => Some("board_missing"),
            Some(b) if b.version != p.version => {
                if registry::version_at_least(&p.version, &b.version) {
                    Some("board_older")
                } else {
                    Some("pc_older")
                }
            }
            Some(b) if b.state != app_state::CARD_MISSING && b.sha != p.sha => {
                Some("board_differs")
            }
            Some(_) => None,
        };
        if let Some(k) = kind {
            out.push(item(id, &p.name, k, &p.version, board.get(id)));
        }
    }
    for (id, b) in board {
        if !pc.contains_key(id) && !kept.contains(id) {
            out.push(item(id, id, "board_only", "", Some(b)));
        }
    }
    out
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
    /// Last time the board took more bytes (or the op started): the timeout counts from here.
    progress: Instant,
    /// Percentage last shown for a big install.
    shown_pct: u8,
}

pub struct Apps {
    pub cfg: Config,
    root: PathBuf,
    installed: BTreeMap<String, Installed>,
    run: BTreeMap<String, Run>,
    gen: u64,
    link: Sender<Msg>,
    inbox: Sender<Cmd>,
    /// Board link up, firmware matches and the whole APP_INDEX received: installs may go ahead.
    board_ready: bool,
    connected: bool,
    firmware_ok: bool,
    on_board: BTreeMap<String, BoardApp>,
    /// APP_INDEX being read: the page asked for, when, and the entries so far.
    index_page: Option<(u8, Instant)>,
    index: Vec<AppIndexEntry>,
    ops: VecDeque<Op>,
    current: Option<Current>,
    open_app: Option<String>,
    /// BIND_SETs not sent yet (app id, binding, value), oldest first; one per binding.
    binds: VecDeque<(String, u16, BindValue)>,
    /// Start of the current pacing window and the values sent in it.
    bind_window: (Instant, u32),
    /// The bound image being sent, and its chunks sent in the current window.
    image: Option<ImageSend>,
    image_chunks: u32,
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

/// What the PC half gets as `settings`: the values plus every `secret` setting in plain text
/// (`""` until the user sets it; P51-M).
fn sidecar_values(m: Option<&Manifest>, c: &AppCfg) -> Map<String, Value> {
    let mut v = c.settings.clone();
    for s in m.map_or(&[][..], |m| &m.settings[..]) {
        if s.get("type").and_then(Value::as_str) != Some("secret") {
            continue;
        }
        if let Some(k) = s.get("key").and_then(Value::as_str) {
            let val = c.secrets.0.get(k).cloned().unwrap_or_else(|| "".into());
            v.insert(k.to_string(), val);
        }
    }
    v
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
            index_page: None,
            index: Vec::new(),
            ops: VecDeque::new(),
            current: None,
            open_app: None,
            binds: VecDeque::new(),
            bind_window: (Instant::now(), 0),
            image: None,
            image_chunks: 0,
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
                let installing = matches!(&self.current, Some(Current { op: Op::Install(x), .. }) if x == id)
                    || self.ops.iter().any(|o| matches!(o, Op::Install(x) if x == id));
                let b = self.on_board.get(id);
                let place = match b {
                    None => "",
                    Some(b) if b.state == app_state::CARD_MISSING => "card_missing",
                    Some(b) if b.place == app_where::SD => "sd",
                    Some(_) => "flash",
                };
                let (status, detail) = if installing {
                    ("installing", self.install_detail(id))
                } else if !c.enabled {
                    ("off", "Off".to_string())
                } else if let Some(e) = r.and_then(|r| r.error.clone()) {
                    ("error", e)
                } else if !self.connected {
                    ("waiting", "On, waiting for the board".into())
                } else if !self.firmware_ok {
                    ("update", "On, update the board first".into())
                } else if !self.board_ready {
                    ("installing", "Checking the board".into())
                } else {
                    let d = match place {
                        "" => "On, not on the board: Sync with board",
                        "card_missing" => "On, on the board's SD card, which is out",
                        "sd" => "On, on the board's SD card",
                        _ => "On, in the board's flash",
                    };
                    ("on", d.into())
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
                    secrets_set: c
                        .secrets
                        .0
                        .iter()
                        .filter(|(_, v)| v.as_str().is_some_and(|s| !s.is_empty()))
                        .map(|(k, _)| k.clone())
                        .collect(),
                    place: place.into(),
                })
            })
            .collect()
    }

    fn install_detail(&self, id: &str) -> String {
        match &self.current {
            Some(c) if matches!(&c.op, Op::Install(x) if x == id) => {
                let size = self.installed.get(id).map_or(0, |i| i.fxp.len());
                if size > SHOW_PROGRESS_OVER {
                    format!("Installing on the board, {}%", c.acked * 100 / size)
                } else {
                    "Installing on the board".into()
                }
            }
            _ => "Waiting to install on the board".into(),
        }
    }

    /// What differs between this PC and the board (empty until the board's index is read).
    pub fn sync_items(&self) -> Vec<SyncItem> {
        if !self.board_ready {
            return Vec::new();
        }
        let pc: BTreeMap<String, PcApp> = self
            .installed
            .iter()
            .map(|(id, i)| {
                (
                    id.clone(),
                    PcApp {
                        name: i.manifest.name.clone(),
                        version: i.manifest.version.clone(),
                        sha: i.sha,
                    },
                )
            })
            .collect();
        let pending = |id: &str| {
            self.ops
                .iter()
                .chain(self.current.as_ref().map(|c| &c.op))
                .any(|o| matches!(o, Op::Install(x) | Op::Remove(x) if x == id))
        };
        sync_diff(&pc, &self.on_board, &self.cfg.kept_on_board)
            .into_iter()
            .filter(|i| !pending(&i.id))
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
                let m = self.installed.get(&id).map(|i| &i.manifest);
                if m.is_some_and(|m| registry::is_secret(m, &key)) {
                    // "" clears it (the UI's Clear): nothing is kept, encrypted or not.
                    if value.as_str().is_some_and(|v| !v.is_empty()) {
                        c.secrets.0.insert(key, value);
                    } else {
                        c.secrets.0.remove(&key);
                    }
                } else {
                    c.settings.insert(key, value);
                }
                let values = sidecar_values(m, c);
                self.save();
                self.to_sidecar(&id, Down::Settings { values });
                true
            }
            Cmd::Button { id, key } => {
                self.to_sidecar(&id, Down::Button { key });
                false
            }
            Cmd::Remove { id } => {
                // Both halves (P51): the board half goes too when the board has it.
                self.set_enabled(&id, false);
                self.ops
                    .retain(|o| !matches!(o, Op::Install(x) if x == &id));
                // An install under way stops (`tick` drops it once the files are gone), but the
                // board may have finished it already: remove it there too (`UNKNOWN_APP` is fine).
                let sending =
                    matches!(&self.current, Some(Current { op: Op::Install(x), .. }) if x == &id);
                if self.on_board.contains_key(&id) || sending {
                    self.ops.push_back(Op::Remove(id.clone()));
                }
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
            Cmd::Sync { id, action } => self.sync(&id, action),
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
        let mut secrets = crate::secret::Secrets::default();
        if let Some(o) = &old {
            for (k, v) in &o.settings {
                if settings.contains_key(k) {
                    settings.insert(k.clone(), v.clone());
                }
            }
            for (k, v) in &o.secrets.0 {
                if registry::is_secret(&m, k) {
                    secrets.0.insert(k.clone(), v.clone());
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
                secrets,
            },
        );
        self.cfg.kept_on_board.remove(&id);
        self.save();
        self.log(false, format!("{} v{} added", m.name, m.version));
        self.set_enabled(&id, enable || was_on);
        // Both halves (P51): the board half goes on even while the app is switched off.
        self.queue_install(&id);
        true
    }

    fn sync(&mut self, id: &str, action: SyncAction) -> bool {
        match action {
            SyncAction::Board => self.queue_install(id),
            SyncAction::Keep => {
                self.cfg.kept_on_board.insert(id.into());
                self.save();
                self.log(false, format!("{id} kept on the board without its PC part"));
                self.send_host_state(id);
            }
            SyncAction::Remove => {
                if self.on_board.contains_key(id)
                    && !self
                        .ops
                        .iter()
                        .any(|o| matches!(o, Op::Remove(x) if x == id))
                {
                    self.ops.push_back(Op::Remove(id.into()));
                }
                self.cfg.kept_on_board.remove(id);
                self.save();
            }
            SyncAction::Pc => {}
        }
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
            self.send_host_state(id);
        } else {
            // The board half stays (P51): its tile fades and offers Turn on.
            if changed {
                self.log(false, format!("{name} switched off"));
            }
            self.stop_sidecar(id);
            self.send_host_state(id);
        }
        true
    }

    fn queue_install(&mut self, id: &str) {
        let Some(i) = self.installed.get(id) else {
            return;
        };
        if self.on_board.get(id).is_some_and(|b| b.sha == i.sha) {
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
        self.binds.clear();
        self.image = None;
        if firmware_ok {
            self.ask_index();
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
            Msg::AppIndex { page, pages, apps } if self.connected && self.firmware_ok => {
                if self.index_page.map(|(p, _)| p) != Some(*page) {
                    return false;
                }
                self.index.extend(apps.iter().cloned());
                if page + 1 < *pages {
                    self.index_page = Some((page + 1, Instant::now()));
                    let _ = self.link.send(Msg::AppIndexReq { page: page + 1 });
                    return false;
                }
                self.index_page = None;
                self.on_board = std::mem::take(&mut self.index)
                    .into_iter()
                    .map(|e| (e.id.clone(), BoardApp::from(e)))
                    .collect();
                if !self.board_ready {
                    self.board_ready = true;
                    self.index_read();
                }
                true
            }
            Msg::AppTurnOn { id } => {
                if self.cfg.apps.contains_key(id) {
                    let name = self.name(id);
                    self.log(false, format!("{name}: Turn on pressed on the board"));
                    self.set_enabled(id, true)
                } else {
                    self.send_host_state(id);
                    false
                }
            }
            Msg::AppAck { next_offset } => {
                let Some(c) = self.current.as_mut() else {
                    return false;
                };
                let next = *next_offset as usize;
                if next > c.acked {
                    c.progress = Instant::now();
                }
                c.acked = next;
                c.last_ack = Instant::now();
                let size = match &c.op {
                    Op::Install(id) => self.installed.get(id).map_or(0, |i| i.fxp.len()),
                    Op::Remove(_) => 0,
                };
                if size <= SHOW_PROGRESS_OVER {
                    return false;
                }
                let pct = (next * 100 / size) as u8;
                let changed = pct != c.shown_pct;
                c.shown_pct = pct;
                changed
            }
            Msg::AppResult {
                op,
                code: rc,
                id,
                text,
            } => self.op_result(*op, *rc, id, text),
            Msg::AppOpened { id } => {
                self.open_app = Some(id.clone());
                self.binds.retain(|(app, ..)| app == id); // the board ignores other apps' values
                if self.image.as_ref().is_some_and(|i| &i.id != id) {
                    self.image = None;
                }
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

    fn ask_index(&mut self) {
        self.index.clear();
        self.index_page = Some((0, Instant::now()));
        let _ = self.link.send(Msg::AppIndexReq { page: 0 });
    }

    /// The board's apps are known after a connect: tell it each one's PC state, and say when the
    /// two sides differ (the user syncs on the Apps page).
    fn index_read(&mut self) {
        let ids: Vec<String> = self.on_board.keys().cloned().collect();
        for id in &ids {
            self.send_host_state(id);
        }
        let n = self.sync_items().len();
        if n > 0 {
            self.log(
                false,
                format!("The board and this PC differ in {n} app(s): Apps › Sync with board"),
            );
        }
    }

    /// The state of an app's PC half as the board shows it (`fenix_proto::host`).
    fn host_state(&self, id: &str) -> u8 {
        let (Some(i), Some(c)) = (self.installed.get(id), self.cfg.apps.get(id)) else {
            return host::NOT_HERE;
        };
        if !c.enabled {
            return host::OFF;
        }
        if i.manifest.pc.is_none() {
            return host::RUNNING;
        }
        match self.run.get(id) {
            Some(r) if r.ready => host::RUNNING,
            Some(r) if r.sidecar.is_some() || r.restart_at.is_some() => host::STARTING,
            _ => host::OFF,
        }
    }

    fn send_host_state(&mut self, id: &str) {
        if !self.board_ready || !self.on_board.contains_key(id) {
            return;
        }
        let _ = self.link.send(Msg::HostState {
            state: self.host_state(id),
            id: id.into(),
        });
    }

    /// Drive the board queue: one install or remove at a time. True when the UI changed.
    pub fn tick(&mut self) -> bool {
        self.flush_binds();
        let mut changed = self.restart_due();
        if let Some((page, at)) = self.index_page {
            if at.elapsed() > INDEX_RETRY && self.connected {
                self.index_page = Some((page, Instant::now()));
                let _ = self.link.send(Msg::AppIndexReq { page });
            }
        }
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
                    let secs = (i.fxp.len() / USB_BYTES_PER_S) as u64;
                    if Duration::from_secs(secs) > SLOW_INSTALL {
                        let (name, mb) = (i.manifest.name.clone(), i.fxp.len() as f32 / 1048576.0);
                        self.log(
                            false,
                            format!("{name} is {mb:.1} MB: installing it on the board takes about {} min over USB", secs.div_ceil(60)),
                        );
                    }
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
                progress: Instant::now(),
                shown_pct: 0,
            });
            changed = true;
        }
        let Some(c) = self.current.as_mut() else {
            return changed;
        };
        if c.progress.elapsed() > OP_TIMEOUT {
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
        self.current.is_some()
            || (self.board_ready && !self.ops.is_empty())
            || !self.binds.is_empty()
            || self.image.is_some()
    }

    /// Send waiting binding values, up to BIND_BURST in each BIND_WINDOW.
    fn flush_binds(&mut self) {
        if self.bind_window.0.elapsed() >= BIND_WINDOW {
            self.bind_window = (Instant::now(), 0);
            self.image_chunks = 0;
        }
        while self.bind_window.1 < BIND_BURST {
            let Some((id, bind, value)) = self.binds.pop_front() else {
                break;
            };
            self.bind_window.1 += 1;
            let _ = self.link.send(Msg::BindSet { bind, id, value });
        }
        while self.image_chunks < IMAGE_CHUNKS {
            let Some(im) = self.image.as_mut() else {
                break;
            };
            let end = (im.offset + IMAGE_CHUNK).min(im.data.len());
            let _ = self.link.send(Msg::BindImage {
                bind: im.bind,
                w: im.w,
                h: im.h,
                total: im.data.len() as u32,
                offset: im.offset as u32,
                id: im.id.clone(),
                data: im.data[im.offset..end].to_vec(),
            });
            im.offset = end;
            self.image_chunks += 1;
            if end == im.data.len() {
                self.image = None;
            }
        }
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
                    let b = BoardApp {
                        version: i.manifest.version.clone(),
                        sha: i.sha,
                        place: app_where::SD,
                        state: app_state::READY,
                    };
                    self.on_board.insert(id.into(), b);
                }
                self.log(
                    false,
                    format!("{name} installed on the board ({secs:.1} s)"),
                );
                self.send_host_state(id);
                self.ask_index(); // where it went (SD card or flash)
            }
            (code::OP_REMOVE, code::OK | code::UNKNOWN_APP) => {
                self.on_board.remove(id);
                self.log(false, format!("{name} removed from the board"));
            }
            (code::OP_INSTALL, _) => {
                let why = match rc {
                    code::NO_SPACE => "the board is full".to_string(),
                    code::TOO_BIG_FOR_FLASH => {
                        "it is over 128 KB and the board has no SD card; put a card in".to_string()
                    }
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
                values: sidecar_values(Some(&i.manifest), c),
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
        self.send_host_state(id); // STARTING until it says ready
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

    /// An answer for the app, if `gen` is still its run.
    pub fn reply(&self, id: &str, gen: u64, m: Down) {
        if self.run.get(id).is_some_and(|r| r.gen == gen) {
            self.to_sidecar(id, m);
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
                self.send_host_state(id);
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
                queue_bind(&mut self.binds, id, b, value);
                self.flush_binds();
                false
            }
            Note::Up(Up::SetImage { bind, w, h, data }) => {
                if self.open_app.as_deref() != Some(id) {
                    return false; // like values: the app sends again on `opened`
                }
                let Some(i) = self.installed.get(id) else {
                    return false;
                };
                let art = i.pkg.art().filter(|a| i.pkg.bind_id(&bind) == Some(a.bind));
                let rgb = fenix_sdk::wire::decode(&data);
                let (w, h) = (w as usize, h as usize);
                let why = match (&art, &rgb) {
                    (None, _) => Some(format!("no bound image named {bind}")),
                    (_, Some(px)) if w > 0 && h > 0 && px.len() == w * h * 3 => None,
                    _ => Some("bad picture data".to_string()),
                };
                if let Some(why) = why {
                    self.log(true, format!("{name}: set_image: {why}"));
                    return false;
                }
                let (art, rgb) = (art.unwrap(), rgb.unwrap());
                let (tw, th) = (art.w as usize, art.h as usize);
                let px = fenix_sdk::image::fit(&rgb, w, h, tw, th);
                self.image = Some(ImageSend {
                    id: id.into(),
                    bind: art.bind,
                    w: art.w,
                    h: art.h,
                    data: fenix_sdk::image::rgb565_le(&px),
                    offset: 0,
                });
                self.flush_binds();
                false
            }
            Note::Up(Up::Log { level, text }) => {
                let problem = level == "warn" || level == "error";
                self.log(problem, format!("{name}: {text}"));
                false
            }
            // Files go to the engine, which has the link's file jobs (`Engine::app_file`).
            Note::Up(
                Up::Unknown | Up::FilePut { .. } | Up::FileGet { .. } | Up::FileDelete { .. },
            ) => false,
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
                self.send_host_state(id); // starting again, or off
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

/// Add a binding value to the waiting ones: a value for a binding already waiting replaces it in
/// its place, so a burst sends each binding once, with its latest value.
fn queue_bind(q: &mut VecDeque<(String, u16, BindValue)>, id: &str, bind: u16, value: BindValue) {
    match q.iter_mut().find(|(a, k, _)| a == id && *k == bind) {
        Some(waiting) => waiting.2 = value,
        None => q.push_back((id.into(), bind, value)),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bind_burst_keeps_the_latest_value_per_binding() {
        let mut q = VecDeque::new();
        for i in 0..50 {
            queue_bind(&mut q, "a", (i % 5) as u16, BindValue::Int(i));
        }
        queue_bind(&mut q, "b", 0, BindValue::Bool(true));
        let got: Vec<_> = q
            .iter()
            .map(|(a, k, v)| (a.as_str(), *k, v.clone()))
            .collect();
        assert_eq!(
            got,
            [
                ("a", 0, BindValue::Int(45)),
                ("a", 1, BindValue::Int(46)),
                ("a", 2, BindValue::Int(47)),
                ("a", 3, BindValue::Int(48)),
                ("a", 4, BindValue::Int(49)),
                ("b", 0, BindValue::Bool(true)),
            ]
        );
    }

    fn pc(list: &[(&str, &str, u8)]) -> BTreeMap<String, PcApp> {
        list.iter()
            .map(|(id, v, sha)| {
                let app = PcApp {
                    name: format!("{id} app"),
                    version: v.to_string(),
                    sha: [*sha; 32],
                };
                (id.to_string(), app)
            })
            .collect()
    }

    fn board(list: &[(&str, &str, u8, u8)]) -> BTreeMap<String, BoardApp> {
        list.iter()
            .map(|(id, v, sha, state)| {
                let app = BoardApp {
                    version: v.to_string(),
                    sha: [*sha; 32],
                    place: app_where::SD,
                    state: *state,
                };
                (id.to_string(), app)
            })
            .collect()
    }

    fn kinds(items: &[SyncItem]) -> Vec<(String, String)> {
        items
            .iter()
            .map(|i| (i.id.clone(), i.kind.clone()))
            .collect()
    }

    const READY: u8 = app_state::READY;
    const MISSING: u8 = app_state::CARD_MISSING;

    #[test]
    fn same_on_both_sides_is_in_sync() {
        let d = sync_diff(
            &pc(&[("a", "1.0.0", 1)]),
            &board(&[("a", "1.0.0", 1, READY)]),
            &BTreeSet::new(),
        );
        assert!(d.is_empty());
    }

    #[test]
    fn pc_only_goes_to_the_board() {
        // Switched off or not, an app added here belongs on the board too.
        let d = sync_diff(
            &pc(&[("a", "1.0.0", 1)]),
            &BTreeMap::new(),
            &BTreeSet::new(),
        );
        assert_eq!(kinds(&d), [("a".into(), "board_missing".into())]);
        assert_eq!(d[0].name, "a app");
    }

    #[test]
    fn board_only_is_listed_unless_kept() {
        let b = board(&[("x", "2.0.0", 9, READY)]);
        let d = sync_diff(&BTreeMap::new(), &b, &BTreeSet::new());
        assert_eq!(kinds(&d), [("x".into(), "board_only".into())]);
        assert_eq!(
            (d[0].name.as_str(), d[0].board_version.as_str()),
            ("x", "2.0.0")
        );
        let kept: BTreeSet<String> = ["x".to_string()].into();
        assert!(sync_diff(&BTreeMap::new(), &b, &kept).is_empty());
    }

    #[test]
    fn version_skew_updates_the_older_side() {
        let d = sync_diff(
            &pc(&[("a", "1.2.0", 1), ("b", "1.0.0", 2)]),
            &board(&[("a", "1.1.0", 3, READY), ("b", "1.10.0", 4, READY)]),
            &BTreeSet::new(),
        );
        assert_eq!(
            kinds(&d),
            [
                ("a".into(), "board_older".into()),
                ("b".into(), "pc_older".into())
            ]
        );
    }

    #[test]
    fn same_version_different_build_goes_to_the_board() {
        let d = sync_diff(
            &pc(&[("a", "1.0.0", 1)]),
            &board(&[("a", "1.0.0", 2, READY)]),
            &BTreeSet::new(),
        );
        assert_eq!(kinds(&d), [("a".into(), "board_differs".into())]);
    }

    #[test]
    fn card_missing_compares_versions_only() {
        // Its sha is unknown (zero) while the card is out.
        let d = sync_diff(
            &pc(&[("a", "1.0.0", 1)]),
            &board(&[("a", "1.0.0", 0, MISSING)]),
            &BTreeSet::new(),
        );
        assert!(d.is_empty());
        let d = sync_diff(
            &BTreeMap::new(),
            &board(&[("x", "1.0.0", 0, MISSING)]),
            &BTreeSet::new(),
        );
        assert!(d[0].card_missing);
    }

    #[test]
    fn sync_action_names() {
        let a: SyncAction = serde_json::from_str("\"remove\"").unwrap();
        assert_eq!(a, SyncAction::Remove);
        let a: SyncAction = serde_json::from_str("\"board\"").unwrap();
        assert_eq!(a, SyncAction::Board);
    }
}
