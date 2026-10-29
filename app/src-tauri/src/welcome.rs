//! First-run experience window (PLAN §3.9). It is its own window with its own WebView2 data
//! folder, so it can run with the GPU on while the main window keeps its lean, GPU-off args
//! (WebView2 shares one browser process, and so one set of args, per data folder).

use tauri::{AppHandle, Manager, WebviewUrl, WebviewWindowBuilder};

const LEAN_ARGS: &str = "--disable-features=msWebOOUI,msPdfOOUI,msSmartScreenProtection --enable-features=NetworkServiceInProcess2 --disable-gpu --in-process-gpu --renderer-process-limit=1 --disable-extensions --disable-background-networking --disable-component-update";
const GPU_ARGS: &str = "--disable-features=msWebOOUI,msPdfOOUI,msSmartScreenProtection --enable-features=NetworkServiceInProcess2 --renderer-process-limit=1 --disable-extensions --disable-background-networking --disable-component-update";

pub fn open(app: &AppHandle) {
    if let Some(w) = app.get_webview_window("welcome") {
        let _ = w.show();
        let _ = w.set_focus();
        return;
    }
    // FENIX_OOTB_ARGS=lean runs the welcome window with the main window's args (S8 comparison).
    let lean = std::env::var("FENIX_OOTB_ARGS").is_ok_and(|v| v == "lean");
    let dir = crate::local_dir().join(if lean {
        "webview-welcome-lean"
    } else {
        "webview-welcome"
    });
    // FENIX_OOTB_FPS=1 shows the frame rate in a corner (S8 / P9 acceptance numbers).
    let page = if std::env::var("FENIX_OOTB_FPS").is_ok_and(|v| v == "1") {
        "welcome.html?fps"
    } else {
        "welcome.html"
    };
    let r = WebviewWindowBuilder::new(app, "welcome", WebviewUrl::App(page.into()))
        .title("Welcome to fenixOS")
        .inner_size(1280.0, 800.0)
        .min_inner_size(720.0, 520.0)
        .center()
        .decorations(false)
        .background_color(tauri::window::Color(0, 0, 0, 255))
        .additional_browser_args(if lean { LEAN_ARGS } else { GPU_ARGS })
        .data_directory(dir)
        .build();
    match r {
        Ok(w) => {
            let _ = w.set_focus();
        }
        Err(e) => eprintln!("welcome window: {e}"),
    }
}
