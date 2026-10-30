//! fenixOS PC app: the engine (board link) runs in the tray; the window is created on demand and
//! destroyed on close, so an idle fenixOS holds no webview (PLAN §3.5, §3.6).
//! `fenixos --quit` quits a running instance; `--tray` (autostart) starts without the window.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod apps;
mod config;
mod engine;
mod logs;
mod registry;
mod sidecar;
mod tray;
mod update;
mod welcome;
mod weather;
mod wifi;

use std::path::PathBuf;
use std::sync::Arc;

use tauri::{AppHandle, Manager, RunEvent, WebviewWindowBuilder};
use tauri_plugin_autostart::{MacosLauncher, ManagerExt};

use apps::Cmd;
use engine::{Engine, State};
use fenix_flash as flash;
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
    // FENIX_WINDOW=background opens the window minimized without taking focus (scripted tests).
    let quiet = std::env::var("FENIX_WINDOW").is_ok_and(|v| v == "background");
    match WebviewWindowBuilder::from_config(app, &cfg).and_then(|b| b.focused(!quiet).build()) {
        Ok(w) if quiet => {
            let _ = w.minimize();
        }
        Ok(w) => {
            let _ = w.set_focus();
        }
        Err(e) => eprintln!("window: {e}"),
    }
}

/// Cores' worth of CPU fenixOS may use at most (user request, P11).
const CPU_CORES: u32 = 4;

/// Hard-cap this process and everything it starts (WebView2, PC halves, flashing) at
/// `CPU_CORES` cores' worth of CPU time, at normal priority. A job object's CPU rate applies to
/// the whole tree; WebView2's own sandbox jobs nest inside it. Threads still run on any core.
fn cap_cpu() {
    use windows_sys::Win32::System::JobObjects::{
        AssignProcessToJobObject, CreateJobObjectW, JobObjectCpuRateControlInformation,
        SetInformationJobObject, JOBOBJECT_CPU_RATE_CONTROL_INFORMATION,
        JOB_OBJECT_CPU_RATE_CONTROL_ENABLE, JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP,
    };
    use windows_sys::Win32::System::Threading::GetCurrentProcess;
    let all = std::thread::available_parallelism().map_or(1, |n| n.get() as u32);
    if all <= CPU_CORES {
        return;
    }
    unsafe {
        let job = CreateJobObjectW(std::ptr::null(), std::ptr::null());
        if job.is_null() {
            return;
        }
        let mut info: JOBOBJECT_CPU_RATE_CONTROL_INFORMATION = std::mem::zeroed();
        info.ControlFlags =
            JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
        // In 1/100 of a percent of all processors.
        info.Anonymous.CpuRate = CPU_CORES * 10_000 / all;
        SetInformationJobObject(
            job,
            JobObjectCpuRateControlInformation,
            &info as *const _ as *const _,
            std::mem::size_of::<JOBOBJECT_CPU_RATE_CONTROL_INFORMATION>() as u32,
        );
        // The handle stays open for the life of the process, so the job does too.
        AssignProcessToJobObject(job, GetCurrentProcess());
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

/// First run: what is on a board that does not answer as fenixOS (resets it into the bootloader).
#[tauri::command]
async fn probe_board(app: AppHandle) -> Result<flash::Probe, String> {
    let e = app.state::<Arc<Engine>>().inner().clone();
    tauri::async_runtime::spawn_blocking(move || e.probe(&app))
        .await
        .map_err(|e| e.to_string())?
}

/// First run: erase the board and install the bundled fenixOS.
#[tauri::command]
fn install_board(app: AppHandle, engine: tauri::State<Arc<Engine>>) -> Result<(), String> {
    engine.inner().start_update(&app, true)
}

/// End the first-run experience: remember it, close its window, open the main one if asked.
/// Async: on Windows a sync command that creates a webview blocks WebView2 and the new window
/// stays black.
#[tauri::command]
async fn finish_welcome(app: AppHandle, open_main: bool) {
    let engine = app.state::<Arc<Engine>>().inner().clone();
    engine.with_apps(|a| {
        a.cfg.welcome_done = true;
        if let Err(e) = a.cfg.save() {
            eprintln!("config: {e}");
        }
    });
    // The welcome already showed the install finishing; the main window need not say it again.
    engine.clear_update(&app);
    if open_main {
        open_window(&app);
    }
    if let Some(w) = app.get_webview_window("welcome") {
        let _ = w.destroy();
    }
}

/// Settings > About > Show welcome again.
#[tauri::command]
async fn show_welcome(app: AppHandle) {
    welcome::open(&app);
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

/// Start or stop the board's stream (Board page); `State.streaming` follows.
#[tauri::command]
fn mirror_set(app: AppHandle, engine: tauri::State<Arc<Engine>>, on: bool) {
    engine.mirror_set(&app, on);
}

/// The Task manager's sample (P27): asks the board for a new one, returns the latest.
#[tauri::command]
fn sysinfo(engine: tauri::State<Arc<Engine>>) -> engine::SysinfoView {
    engine.sysinfo()
}

/// Switch the board's radio (P32): 0 off, 1 Bluetooth, 2 Wi-Fi. The board restarts.
#[tauri::command]
fn set_radio(app: AppHandle, engine: tauri::State<Arc<Engine>>, mode: u8) {
    engine.radio_set(&app, mode);
}

/// Start a Wi-Fi scan (board in Wi-Fi mode, else this PC); poll `get_wifi_scan`.
#[tauri::command]
fn wifi_scan(engine: tauri::State<Arc<Engine>>) {
    engine.wifi_scan_start();
}

#[tauri::command]
fn get_wifi_scan(engine: tauri::State<Arc<Engine>>) -> engine::WifiScanView {
    engine.wifi_scan()
}

/// Save a network on the board and connect (empty SSID forgets it).
#[tauri::command]
fn wifi_join(app: AppHandle, engine: tauri::State<Arc<Engine>>, ssid: String, password: String) {
    engine.wifi_join(&app, ssid, password);
}

/// Settings > Weather: find places by name (Open-Meteo geocoding).
#[tauri::command]
async fn weather_search(query: String) -> Result<Vec<weather::Place>, String> {
    tauri::async_runtime::spawn_blocking(move || weather::search(&query))
        .await
        .map_err(|e| e.to_string())?
}

#[tauri::command]
fn get_weather_place(engine: tauri::State<Arc<Engine>>) -> Option<weather::Place> {
    engine.weather_place()
}

#[tauri::command]
fn set_weather_place(
    app: AppHandle,
    engine: tauri::State<Arc<Engine>>,
    place: Option<weather::Place>,
) -> Result<(), String> {
    engine.weather_set(&app, place)
}

/// A click on the mirror, in board screen coordinates.
#[tauri::command]
fn mirror_touch(engine: tauri::State<Arc<Engine>>, x: u16, y: u16, down: bool) {
    engine.send(fenix_proto::Msg::Touch { x, y, down });
}

/// Flash the bundled firmware (update dialog, Reinstall firmware).
#[tauri::command]
fn update_board(app: AppHandle, engine: tauri::State<Arc<Engine>>) -> Result<(), String> {
    engine.inner().start_update(&app, false)
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
    cap_cpu();
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
            set_radio,
            wifi_scan,
            get_wifi_scan,
            wifi_join,
            weather_search,
            get_weather_place,
            set_weather_place,
            open_url,
            list_registry,
            add_app,
            set_app_enabled,
            set_app_setting,
            app_button,
            remove_app,
            mirror_frame,
            mirror_set,
            mirror_touch,
            sysinfo,
            update_board,
            clear_update,
            list_ports,
            set_port,
            get_app_update,
            check_app_update,
            install_app_update,
            probe_board,
            install_board,
            finish_welcome,
            show_welcome
        ])
        .setup(move |app| {
            // Reached only by the first instance: `--quit` with nothing running is a no-op.
            if std::env::args().any(|a| a == "--quit") {
                std::process::exit(0);
            }
            let logs = logs::Logs::new(local_dir().join("logs"));
            let engine = Engine::start(app.handle().clone(), logs);
            let (check, welcome_done) =
                engine.with_apps(|a| (a.cfg.check_updates, a.cfg.welcome_done));
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
            // First launch (or `--welcome`): the first-run experience instead of the main window.
            // Autostart (`--tray`) never shows it.
            if std::env::args().any(|a| a == "--welcome") || (!welcome_done && !tray_only) {
                welcome::open(app.handle());
            } else if !tray_only {
                open_window(app.handle());
            }
            Ok(())
        })
        .build(tauri::generate_context!())
        .expect("build fenixOS");
    app.run(|app, e| match e {
        // Closing the window keeps fenixOS in the tray; only Quit (exit code set) ends it.
        RunEvent::ExitRequested {
            code: None, api, ..
        } => api.prevent_exit(),
        // The process ends right after this without dropping managed state, so close the serial
        // port cleanly first (a read cancelled by process exit crashed the CH340 driver).
        RunEvent::Exit => {
            if let Some(engine) = app.try_state::<Arc<Engine>>() {
                engine.shutdown();
            }
        }
        _ => {}
    });
}
