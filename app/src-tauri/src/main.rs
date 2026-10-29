//! fenixOS PC app: the engine (board link) runs in the tray; the window is created on demand and
//! destroyed on close, so an idle fenixOS holds no webview (PLAN §3.5, §3.6).
//! `fenixos --quit` quits a running instance; `--tray` (autostart) starts without the window.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod apps;
mod config;
mod engine;
mod flash;
mod logs;
mod registry;
mod sidecar;
mod tray;
mod update;
mod welcome;

use std::path::PathBuf;
use std::sync::Arc;

use tauri::{AppHandle, Manager, RunEvent, WebviewWindowBuilder};
use tauri_plugin_autostart::{MacosLauncher, ManagerExt};

use apps::Cmd;
use engine::{Engine, State};
use logs::Source;
use serde::Serialize;

/// Show the main window, creating its webview if it was closed.
pub fn open_window(app: &AppHandle) {
    if let Some(w) = app.get_webview_window("main") {
        let _ = w.unminimize();
        let _ = w.show();
        let _ = w.set_focus();
        return;
    }
    let Some(cfg) = app
        .config()
        .app
        .windows
        .iter()
        .find(|w| w.label == "main")
        .cloned()
    else {
        return;
    };
    match WebviewWindowBuilder::from_config(app, &cfg).and_then(|b| b.build()) {
        Ok(w) => {
            let _ = w.set_focus();
        }
        Err(e) => eprintln!("window: {e}"),
    }
}

fn local_dir() -> PathBuf {
    std::env::var_os("LOCALAPPDATA")
        .map(PathBuf::from)
        .unwrap_or_else(std::env::temp_dir)
        .join("fenixOS")
}

// ---- Commands (the UI's only way in) ----

#[tauri::command]
fn get_state(engine: tauri::State<Arc<Engine>>) -> State {
    engine.snapshot()
}

#[tauri::command]
fn get_logs(engine: tauri::State<Arc<Engine>>) -> Vec<logs::Line> {
    engine.logs.recent()
}

#[tauri::command]
fn open_log_folder(engine: tauri::State<Arc<Engine>>) {
    let _ = std::process::Command::new("explorer.exe")
        .arg(engine.logs.dir())
        .spawn();
}

#[tauri::command]
fn get_autostart(app: AppHandle) -> bool {
    app.autolaunch().is_enabled().unwrap_or(false)
}

#[tauri::command]
fn set_autostart(app: AppHandle, on: bool) -> Result<bool, String> {
    let a = app.autolaunch();
    if on { a.enable() } else { a.disable() }.map_err(|e| e.to_string())?;
    a.is_enabled().map_err(|e| e.to_string())
}

/// One serial port for Settings > Connection port.
#[derive(Serialize)]
struct PortItem {
    name: String,
    product: String,
    /// A CH340 (the board's USB chip): Automatic tries only these.
    board: bool,
}

#[derive(Serialize)]
struct Ports {
    /// The saved choice; `None` is Automatic.
    selected: Option<String>,
    ports: Vec<PortItem>,
}

#[tauri::command]
fn list_ports(engine: tauri::State<Arc<Engine>>) -> Ports {
    let mut ports: Vec<PortItem> = fenix_link::list_ports()
        .into_iter()
        .map(|(p, board)| PortItem {
            name: p.name,
            product: p.product.unwrap_or_default(),
            board,
        })
        .collect();
    // COM9 after COM10 reads wrong in a menu: sort by the number.
    ports.sort_by_key(|p| {
        p.name
            .trim_start_matches("COM")
            .parse::<u32>()
            .unwrap_or(u32::MAX)
    });
    Ports {
        selected: engine.port_setting(),
        ports,
    }
}

#[tauri::command]
fn set_port(
    app: AppHandle,
    engine: tauri::State<Arc<Engine>>,
    port: Option<String>,
) -> Result<(), String> {
    engine.set_port(&app, port)
}

/// Settings > Updates: the last check (the app checks once at start).
#[tauri::command]
fn get_app_update(updates: tauri::State<update::Updates>) -> update::AppUpdate {
    updates.0.lock().unwrap().clone()
}

#[tauri::command]
async fn check_app_update(app: AppHandle) -> update::AppUpdate {
    update::check(&app, false).await
}

#[tauri::command]
async fn install_app_update(app: AppHandle) -> Result<(), String> {
    update::install(&app).await
}

#[tauri::command]
fn reboot_board(app: AppHandle, engine: tauri::State<Arc<Engine>>) {
    engine.log(&app, Source::Pc, false, "Asked the board to restart");
    engine.send(fenix_proto::Msg::Reboot);
}

/// The board screen for the Board page: a 16-byte header (version u64, width u16, height u16,
/// 4 bytes 0), then RGB565 pixels (little-endian) only when newer than `since`.
#[tauri::command]
fn mirror_frame(engine: tauri::State<Arc<Engine>>, since: u64) -> tauri::ipc::Response {
    engine.mirror_poll();
    let m = engine.mirror.lock().unwrap();
    let c = &m.canvas;
    let mut out = Vec::with_capacity(16 + c.px.len() * 2);
    out.extend_from_slice(&m.version.to_le_bytes());
    out.extend_from_slice(&c.width.to_le_bytes());
    out.extend_from_slice(&c.height.to_le_bytes());
    out.extend_from_slice(&[0; 4]);
    if m.version != since {
        for p in &c.px {
            out.extend_from_slice(&p.to_le_bytes());
        }
    }
    tauri::ipc::Response::new(out)
}

#[tauri::command]
fn mirror_stop(engine: tauri::State<Arc<Engine>>) {
    engine.mirror_stop();
}

/// A click on the mirror, in board screen coordinates.
#[tauri::command]
fn mirror_touch(engine: tauri::State<Arc<Engine>>, x: u16, y: u16, down: bool) {
    engine.send(fenix_proto::Msg::Touch { x, y, down });
}

/// Flash the bundled firmware (update dialog, Reinstall firmware).
#[tauri::command]
fn update_board(app: AppHandle, engine: tauri::State<Arc<Engine>>) -> Result<(), String> {
    engine.inner().start_update(&app)
}

#[tauri::command]
fn clear_update(app: AppHandle, engine: tauri::State<Arc<Engine>>) {
    engine.clear_update(&app);
}

/// One registry app for Add app.
#[derive(Serialize)]
struct RegistryItem {
    #[serde(flatten)]
    manifest: registry::Manifest,
    /// This exact version is already added.
    added: bool,
}

/// Read the registry (GitHub, or `FENIX_REGISTRY`). Slow: runs off the UI thread.
#[tauri::command]
async fn list_registry(app: AppHandle) -> Result<Vec<RegistryItem>, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let engine = app.state::<Arc<Engine>>();
        let (apps, bad) = registry::fetch(&registry::source())?;
        for b in bad {
            engine.log(
                &app,
                Source::Pc,
                true,
                format!("Registry entry skipped: {b}"),
            );
        }
        *engine.registry.lock().unwrap() = apps.clone();
        Ok(apps
            .into_iter()
            .map(|m| RegistryItem {
                added: engine.with_apps(|a| a.is_installed(&m.id, &m.version)),
                manifest: m,
            })
            .collect())
    })
    .await
    .map_err(|e| e.to_string())?
}

/// Download, check and add an app from the last registry read, then switch it on.
#[tauri::command]
async fn add_app(app: AppHandle, id: String) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        let engine = app.state::<Arc<Engine>>();
        let m = engine
            .registry
            .lock()
            .unwrap()
            .iter()
            .find(|m| m.id == id)
            .cloned()
            .ok_or("not in the registry")?;
        engine.log(
            &app,
            Source::Apps,
            false,
            format!("Downloading {} v{}", m.name, m.version),
        );
        if let Err(e) = apps::fetch_app(&registry::source(), &m) {
            engine.log(
                &app,
                Source::Apps,
                true,
                format!("{} could not be added: {e}", m.name),
            );
            return Err(e);
        }
        engine.post(Cmd::Added {
            manifest: Box::new(m),
            enable: true,
        });
        Ok(())
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
fn set_app_enabled(engine: tauri::State<Arc<Engine>>, id: String, on: bool) {
    engine.post(Cmd::SetEnabled { id, on });
}

#[tauri::command]
fn set_app_setting(
    engine: tauri::State<Arc<Engine>>,
    id: String,
    key: String,
    value: serde_json::Value,
) {
    engine.post(Cmd::SetSetting { id, key, value });
}

#[tauri::command]
fn app_button(engine: tauri::State<Arc<Engine>>, id: String, key: String) {
    engine.post(Cmd::Button { id, key });
}

#[tauri::command]
fn remove_app(engine: tauri::State<Arc<Engine>>, id: String) {
    engine.post(Cmd::Remove { id });
}

/// Open an https link in the default browser (links never load inside the app window).
#[tauri::command]
fn open_url(url: String) -> Result<(), String> {
    if !url.starts_with("https://") || url.contains('"') {
        return Err("only https links".into());
    }
    std::process::Command::new("rundll32.exe")
        .args(["url.dll,FileProtocolHandler", &url])
        .spawn()
        .map(|_| ())
        .map_err(|e| e.to_string())
}

fn main() {
    let tray_only = std::env::args().any(|a| a == "--tray");
    let app = tauri::Builder::default()
        // First: a second launch hands its arguments to the running instance and exits.
        .plugin(tauri_plugin_single_instance::init(|app, args, _cwd| {
            if args.iter().any(|a| a == "--quit") {
                app.exit(0);
            } else {
                open_window(app);
            }
        }))
        .plugin(tauri_plugin_updater::Builder::new().build())
        .plugin(tauri_plugin_autostart::init(
            MacosLauncher::LaunchAgent,
            Some(vec!["--tray"]),
        ))
        .invoke_handler(tauri::generate_handler![
            get_state,
            get_logs,
            open_log_folder,
            get_autostart,
            set_autostart,
            reboot_board,
            open_url,
            list_registry,
            add_app,
            set_app_enabled,
            set_app_setting,
            app_button,
            remove_app,
            mirror_frame,
            mirror_stop,
            mirror_touch,
            update_board,
            clear_update,
            list_ports,
            set_port,
            get_app_update,
            check_app_update,
            install_app_update
        ])
        .setup(move |app| {
            // Reached only by the first instance: `--quit` with nothing running is a no-op.
            if std::env::args().any(|a| a == "--quit") {
                std::process::exit(0);
            }
            let logs = logs::Logs::new(local_dir().join("logs"));
            let engine = Engine::start(app.handle().clone(), logs);
            let check = engine.with_apps(|a| a.cfg.check_updates);
            app.manage(engine);
            app.manage(update::Updates::new());
            if check {
                let h = app.handle().clone();
                tauri::async_runtime::spawn(async move {
                    update::check(&h, true).await;
                });
            }
            tray::create(app.handle())?;
            // Created last, so every command it calls finds the managed state (PoC gotcha).
            if std::env::args().any(|a| a == "--welcome") {
                welcome::open(app.handle());
            } else if !tray_only {
                open_window(app.handle());
            }
            Ok(())
        })
        .build(tauri::generate_context!())
        .expect("build fenixOS");
    app.run(|_app, e| {
        // Closing the window keeps fenixOS in the tray; only Quit (exit code set) ends it.
        if let RunEvent::ExitRequested {
            code: None, api, ..
        } = e
        {
            api.prevent_exit();
        }
    });
}
