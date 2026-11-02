//! PC half of the starter app (docs/fenixos/pc-half.md): counts seconds and shows them on the
//! board while the app is open. The board's switch pauses the count, its button resets it, and the
//! settings set the greeting and the step. The count is kept on the board's SD card when the
//! screen closes, and read back when the board connects.
#![windows_subsystem = "windows"]

use std::time::{Duration, Instant};

use fenix_sdk::{App, Message};

fn main() {
    let Ok(app) = App::connect() else {
        std::process::exit(2); // started by hand, not by fenixOS
    };
    app.log_info(format!("Starter v{} started", app.version));
    let mut count = 0i32;
    let mut running = true;
    let mut next = Instant::now();
    loop {
        match app.next(next.saturating_duration_since(Instant::now())) {
            Some(Message::Stop) => break,
            // A board without a card (or an older fenixOS) answers an error: start from 0.
            Some(Message::Board { connected: true }) => {
                if let Ok(d) = app.get_file("count.txt") {
                    count = String::from_utf8_lossy(&d).trim().parse().unwrap_or(0);
                }
            }
            Some(Message::Closed) => {
                let _ = app.put_file("count.txt", count.to_string().as_bytes());
            }
            // The board keeps no values while closed: send all of them when it opens.
            Some(Message::Opened) | Some(Message::Settings(_)) => show(&app, count, running),
            Some(Message::Event { name, value, .. }) => {
                match name.as_str() {
                    "reset" => count = 0,
                    "running" => running = value != 0,
                    _ => {}
                }
                show(&app, count, running);
            }
            _ => {}
        }
        if Instant::now() >= next {
            next = Instant::now() + Duration::from_secs(step(&app));
            if running {
                count = (count + 1) % 61;
                show(&app, count, running);
            }
        }
    }
}

/// The "step" setting (a choice: "1", "2" or "5" seconds).
fn step(app: &App) -> u64 {
    app.settings()
        .get("step")
        .and_then(|v| v.as_str()?.parse().ok())
        .unwrap_or(1)
}

fn show(app: &App, count: i32, running: bool) {
    if !app.is_open() {
        return; // fenixOS drops values for a closed app
    }
    let greeting = app.settings().get("greeting").and_then(|v| v.as_str().map(String::from));
    app.set("greeting", greeting.unwrap_or_default());
    app.set("count", count);
    app.set("count_text", format!("{count} s"));
    app.set("running", running);
}
