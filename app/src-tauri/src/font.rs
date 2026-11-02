//! Chinese, Japanese and Korean text on the board (P54, `docs/fonts.md`). The Board page's
//! Install downloads Noto Sans CJK once (pinned URL and sha256, kept in
//! `%LOCALAPPDATA%\fenixOS\fonts`), draws it into the board's bitmap format (`fenix-font`) and
//! sends it to `fonts/cjk.fxf` on the board's SD card. Progress goes to the UI as `font_progress`.

use std::io::Read;
use std::path::PathBuf;
use std::time::Duration;

use serde::Serialize;
use sha2::{Digest, Sha256};
use tauri::AppHandle;

use crate::engine::{emit_ui, Engine};

#[derive(Serialize, Clone)]
struct Progress {
    /// "download", "drawing" or "sending".
    stage: &'static str,
    percent: u8,
}

fn progress(app: &AppHandle, stage: &'static str, percent: u8) {
    emit_ui(app, "font_progress", Progress { stage, percent });
}

fn source_path() -> PathBuf {
    crate::local_dir()
        .join("fonts")
        .join("NotoSansCJKsc-Regular.otf")
}

fn sha_hex(b: &[u8]) -> String {
    crate::registry::hex(&Sha256::digest(b))
}

/// The source font: the cached copy when its sha256 matches, else downloaded and cached.
fn source(app: &AppHandle) -> Result<Vec<u8>, String> {
    let path = source_path();
    if let Ok(d) = std::fs::read(&path) {
        if sha_hex(&d) == fenix_font::NOTO_SHA256 {
            return Ok(d);
        }
    }
    progress(app, "download", 0);
    let agent = ureq::Agent::config_builder()
        .timeout_global(Some(Duration::from_secs(600)))
        .user_agent(concat!("fenixOS/", env!("CARGO_PKG_VERSION")))
        .build()
        .new_agent();
    let url = fenix_font::NOTO_URL;
    let mut resp = agent
        .get(url)
        .call()
        .map_err(|e| format!("download: {e}"))?;
    let total = fenix_font::NOTO_SIZE;
    let mut reader = resp.body_mut().with_config().limit(total + 1).reader();
    let mut data = Vec::with_capacity(total as usize);
    let mut buf = vec![0u8; 64 * 1024];
    let mut shown = 0;
    loop {
        let n = reader
            .read(&mut buf)
            .map_err(|e| format!("download: {e}"))?;
        if n == 0 {
            break;
        }
        data.extend_from_slice(&buf[..n]);
        let pct = (data.len() as u64 * 100 / total).min(100) as u8;
        if pct != shown {
            shown = pct;
            progress(app, "download", pct);
        }
    }
    if sha_hex(&data) != fenix_font::NOTO_SHA256 {
        return Err("the downloaded font is not the expected file (sha256 differs)".into());
    }
    if let Some(dir) = path.parent() {
        let _ = std::fs::create_dir_all(dir);
    }
    let _ = std::fs::write(&path, &data); // a cache: without it the next install downloads again
    Ok(data)
}

/// Size of `fonts/cjk.fxf` on the board's card; `None` when it is not there.
pub fn installed(e: &Engine) -> Result<Option<u32>, String> {
    let entries = match e.board_files(|s, r| fenix_link::files::list(s, r, "fonts")) {
        Ok(v) => v,
        Err(_) => return Ok(None), // no fonts folder yet (or no card: the page hides the row)
    };
    let name = fenix_font::BOARD_PATH.trim_start_matches("fonts/");
    Ok(entries.iter().find(|f| f.name == name).map(|f| f.size))
}

/// Download (once), draw and send the font. Blocks for minutes: the card takes ~13 MB over the link.
pub fn install(e: &Engine, app: &AppHandle) -> Result<(), String> {
    let otf = source(app)?;
    progress(app, "drawing", 0);
    let fxf = fenix_font::build(&otf, &fenix_font::SIZES, None, &mut |p| {
        progress(app, "drawing", p)
    })?;
    drop(otf);
    progress(app, "sending", 0);
    e.board_files(|s, r| {
        fenix_link::files::write_with(s, r, fenix_font::BOARD_PATH, fxf, &mut |p| {
            progress(app, "sending", p)
        })
    })
}

pub fn remove(e: &Engine) -> Result<(), String> {
    e.board_files(|s, r| fenix_link::files::delete(s, r, fenix_font::BOARD_PATH))
}
