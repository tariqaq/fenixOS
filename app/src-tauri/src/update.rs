//! App updates (PLAN §3.8, artboard 13): the Tauri updater reads `latest.json` from the newest
//! GitHub release and checks its minisign signature against the public key in `tauri.conf.json`.
//! The private key never enters the repo (`docs/toolchain.md`, Updater key). After the app
//! updates, the board firmware mismatch dialog offers the matching firmware.

use std::sync::Mutex;

use serde::Serialize;
use tauri::{AppHandle, Manager};
use tauri_plugin_updater::{Error, UpdaterExt};

use crate::logs::Source;

#[derive(Debug, Clone, Default, Serialize)]
pub struct AppUpdate {
    /// `idle`, `checking`, `current`, `available`, `installing` or `failed`.
    pub stage: String,
    /// The newer version when `available` or `installing`.
    pub version: String,
    pub percent: u8,
    pub error: String,
}

/// Managed state: the last check, for the Settings page.
pub struct Updates(pub Mutex<AppUpdate>);

impl Updates {
    pub fn new() -> Self {
        Self(Mutex::new(AppUpdate {
            stage: "idle".into(),
            ..Default::default()
        }))
    }
}

fn set(app: &AppHandle, u: AppUpdate) {
    *app.state::<Updates>().0.lock().unwrap() = u.clone();
    crate::engine::emit_ui(app, "app_update", u);
}

fn log(app: &AppHandle, problem: bool, text: String) {
    if let Some(e) = app.try_state::<std::sync::Arc<crate::engine::Engine>>() {
        e.log(app, Source::Pc, problem, text);
    }
}

/// Ask GitHub for a newer release. `quiet` (the check at start) logs only a found update.
pub async fn check(app: &AppHandle, quiet: bool) -> AppUpdate {
    let busy = app.state::<Updates>().0.lock().unwrap().stage == "installing";
    if busy {
        return app.state::<Updates>().0.lock().unwrap().clone();
    }
    set(
        app,
        AppUpdate {
            stage: "checking".into(),
            ..Default::default()
        },
    );
    let r = match app.updater() {
        Ok(u) => u.check().await,
        Err(e) => Err(e),
    };
    let u = match r {
        Ok(Some(up)) => {
            log(app, false, format!("fenixOS v{} is available", up.version));
            AppUpdate {
                stage: "available".into(),
                version: up.version,
                ..Default::default()
            }
        }
        // No release published yet (404) counts as up to date.
        Ok(None) | Err(Error::ReleaseNotFound) => {
            if !quiet {
                log(app, false, "fenixOS is up to date".into());
            }
            AppUpdate {
                stage: "current".into(),
                ..Default::default()
            }
        }
        Err(e) => {
            if !quiet {
                log(app, true, format!("Update check failed: {e}"));
            }
            AppUpdate {
                stage: "failed".into(),
                error: e.to_string(),
                ..Default::default()
            }
        }
    };
    set(app, u.clone());
    u
}

/// Download, verify and run the new installer; the app quits while it installs and the
/// installer starts it again.
pub async fn install(app: &AppHandle) -> Result<(), String> {
    // The installer step ends the process without RunEvent::Exit: close the board link first
    // (Link::shutdown, the CH340 driver crash), then Tauri's own cleanup, which this replaces.
    let h = app.clone();
    let up = app
        .updater_builder()
        .on_before_exit(move || {
            if let Some(engine) = h.try_state::<std::sync::Arc<crate::engine::Engine>>() {
                engine.shutdown();
            }
            h.cleanup_before_exit();
        })
        .build()
        .map_err(|e| e.to_string())?
        .check()
        .await
        .map_err(|e| e.to_string())?
        .ok_or("fenixOS is already up to date")?;
    let version = up.version.clone();
    log(app, false, format!("Installing fenixOS v{version}"));
    set(
        app,
        AppUpdate {
            stage: "installing".into(),
            version: version.clone(),
            ..Default::default()
        },
    );
    let (mut got, a) = (0usize, app.clone());
    let v = version.clone();
    let r = up
        .download_and_install(
            move |chunk, total| {
                got += chunk;
                if let Some(t) = total.filter(|t| *t > 0) {
                    let percent = (got as u64 * 100 / t).min(100) as u8;
                    let cur = a.state::<Updates>().0.lock().unwrap().percent;
                    if percent != cur {
                        set(
                            &a,
                            AppUpdate {
                                stage: "installing".into(),
                                version: v.clone(),
                                percent,
                                ..Default::default()
                            },
                        );
                    }
                }
            },
            || {},
        )
        .await;
    match r {
        Ok(()) => {
            // On Windows the installer has already closed the app; this covers other cases.
            app.restart();
        }
        Err(e) => {
            log(app, true, format!("App update failed: {e}"));
            set(
                app,
                AppUpdate {
                    stage: "failed".into(),
                    version,
                    error: e.to_string(),
                    ..Default::default()
                },
            );
            Err(e.to_string())
        }
    }
}
