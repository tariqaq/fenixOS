//! Tray icon and menu (PcTray artboard): status header, Open fenixOS, Quit fenixOS.
//! Left click opens the window. The menu is the native Windows menu: a styled one would need its
//! own webview (~60 MB) for three rows (docs/decisions.md).

use tauri::menu::{Menu, MenuItem, PredefinedMenuItem};
use tauri::tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent};
use tauri::{AppHandle, Manager, Wry};

use crate::engine::{State, Status};

pub struct TrayItems {
    status: MenuItem<Wry>,
    detail: MenuItem<Wry>,
}

pub fn create(app: &AppHandle) -> tauri::Result<()> {
    let status = MenuItem::with_id(app, "status", "Board not connected", false, None::<&str>)?;
    let detail = MenuItem::with_id(app, "detail", "Looking for the board", false, None::<&str>)?;
    let open = MenuItem::with_id(app, "open", "Open fenixOS", true, None::<&str>)?;
    let quit = MenuItem::with_id(app, "quit", "Quit fenixOS", true, None::<&str>)?;
    let sep1 = PredefinedMenuItem::separator(app)?;
    let sep2 = PredefinedMenuItem::separator(app)?;
    let menu = Menu::with_items(app, &[&status, &detail, &sep1, &open, &sep2, &quit])?;
    app.manage(TrayItems { status, detail });
    TrayIconBuilder::with_id("main")
        .icon(tauri::image::Image::from_bytes(include_bytes!(
            "../icons/tray.png"
        ))?)
        .tooltip("fenixOS")
        .menu(&menu)
        .show_menu_on_left_click(false)
        .on_menu_event(|app, e| match e.id().as_ref() {
            "open" => crate::open_window(app),
            "quit" => app.exit(0),
            _ => {}
        })
        .on_tray_icon_event(|tray, e| {
            if let TrayIconEvent::Click {
                button: MouseButton::Left,
                button_state: MouseButtonState::Up,
                ..
            } = e
            {
                crate::open_window(tray.app_handle());
            }
        })
        .build(app)?;
    Ok(())
}

/// Status header text, kept in step with the engine state.
pub fn update(app: &AppHandle, s: &State) {
    let Some(items) = app.try_state::<TrayItems>() else {
        return;
    };
    let (status, detail, tip) = match (&s.status, &s.board) {
        (Status::Connected, Some(b)) => (
            "Board connected".to_string(),
            format!("{}, fenixOS v{}", b.port, b.firmware),
            format!("fenixOS: board connected on {}", b.port),
        ),
        _ => (
            "Board not connected".to_string(),
            "Looking for the board".to_string(),
            "fenixOS: board not connected".to_string(),
        ),
    };
    let _ = items.status.set_text(status);
    let _ = items.detail.set_text(detail);
    if let Some(t) = app.tray_by_id("main") {
        let _ = t.set_tooltip(Some(tip));
    }
}
