//! Board firmware flasher (PLAN §3.5): writes the bundled image (`resources/firmware`, made by
//! `tools/bundle_firmware.py`) with the espflash library. espflash resets the board into its
//! bootloader through DTR/RTS, writes only the regions that changed, verifies them, and resets
//! the board again. The app store and settings partitions are not part of the image.

use std::path::{Path, PathBuf};

use espflash::connection::{Connection, ResetAfterOperation, ResetBeforeOperation};
use espflash::flasher::Flasher;
use espflash::image_format::Segment;
use espflash::target::{Chip, ProgressCallbacks};
use serde::Deserialize;
use sha2::{Digest, Sha256};

/// Same speed as `board/idf.ps1 -b 460800`, proven on the CH340C.
const FLASH_BAUD: u32 = 460_800;

#[derive(Deserialize)]
struct Meta {
    version: String,
    files: Vec<Part>,
}

#[derive(Deserialize)]
struct Part {
    offset: u32,
    file: String,
    size: usize,
    sha256: String,
}

pub struct Image {
    pub version: String,
    parts: Vec<(u32, Vec<u8>)>,
}

/// Where the bundled firmware lives: `FENIX_FIRMWARE=<folder>`, else the app's resources.
pub fn dir(resources: Option<PathBuf>) -> Option<PathBuf> {
    match std::env::var_os("FENIX_FIRMWARE") {
        Some(p) if !p.is_empty() => Some(PathBuf::from(p)),
        _ => resources.map(|r| r.join("firmware")),
    }
}

/// The bundled version, without reading the image files.
pub fn bundled_version(dir: &Path) -> Option<String> {
    let meta: Meta =
        serde_json::from_slice(&std::fs::read(dir.join("firmware.json")).ok()?).ok()?;
    Some(meta.version)
}

/// Load the image and check every file against `firmware.json`.
pub fn load(dir: &Path) -> Result<Image, String> {
    let meta: Meta = std::fs::read(dir.join("firmware.json"))
        .map_err(|e| format!("no bundled firmware ({e})"))
        .and_then(|b| serde_json::from_slice(&b).map_err(|e| format!("firmware.json: {e}")))?;
    let mut parts = Vec::new();
    for p in meta.files {
        let data = std::fs::read(dir.join(&p.file)).map_err(|e| format!("{}: {e}", p.file))?;
        let sha = crate::registry::hex(&Sha256::digest(&data));
        if data.len() != p.size || sha != p.sha256 {
            return Err(format!("{}: does not match firmware.json", p.file));
        }
        parts.push((p.offset, data));
    }
    Ok(Image {
        version: meta.version,
        parts,
    })
}

/// Turns espflash's per-segment chunk counts into one 0..=1 fraction over the whole image.
/// espflash writes the segments in order and calls `finish` once per segment, also for one it
/// skips as unchanged (without `init`), so the segment index advances in `finish`.
struct Progress<'a> {
    sizes: Vec<usize>,
    total: usize,
    index: usize,
    chunks: usize,
    report: &'a mut dyn FnMut(f32),
}

impl Progress<'_> {
    fn done_before(&self) -> usize {
        self.sizes[..self.index.min(self.sizes.len())].iter().sum()
    }
    fn size(&self) -> usize {
        self.sizes.get(self.index).copied().unwrap_or(0)
    }
}

impl ProgressCallbacks for Progress<'_> {
    fn init(&mut self, _addr: u32, chunks: usize) {
        self.chunks = chunks.max(1);
        let f = self.done_before() as f32 / self.total as f32;
        (self.report)(f);
    }
    fn update(&mut self, chunk: usize) {
        let done = self.done_before() + self.size() * chunk / self.chunks;
        (self.report)(done as f32 / self.total as f32);
    }
    fn verifying(&mut self) {}
    fn finish(&mut self, _skipped: bool) {
        self.index += 1;
        let f = self.done_before() as f32 / self.total as f32;
        (self.report)(f);
    }
}

/// Write the image to the board on `port`. Blocks for up to a minute; call it off the UI and
/// engine threads, with the board link paused (the port must be free).
pub fn flash(port: &str, img: &Image, report: &mut dyn FnMut(f32)) -> Result<(), String> {
    let info = serialport::available_ports()
        .map_err(|e| e.to_string())?
        .into_iter()
        .find(|p| p.port_name.eq_ignore_ascii_case(port))
        .and_then(|p| match p.port_type {
            serialport::SerialPortType::UsbPort(u) => Some(u),
            _ => None,
        })
        .ok_or_else(|| format!("{port} is not a USB serial port"))?;
    let serial = serialport::new(port, 115_200)
        .flow_control(serialport::FlowControl::None)
        .open_native()
        .map_err(|e| format!("{port}: {e}"))?;
    let conn = Connection::new(
        serial,
        info,
        ResetAfterOperation::HardReset,
        ResetBeforeOperation::DefaultReset,
        115_200,
    );
    let mut flasher = Flasher::connect(conn, true, true, true, Some(Chip::Esp32), Some(FLASH_BAUD))
        .map_err(|e| format!("could not reach the bootloader: {e}"))?;
    let segments: Vec<Segment> = img
        .parts
        .iter()
        .map(|(addr, data)| Segment {
            addr: *addr,
            data: padded(data).into(),
        })
        .collect();
    // Segments are written in address order; `parts` comes sorted from bundle_firmware.py.
    let mut progress = Progress {
        sizes: img.parts.iter().map(|(_, d)| d.len()).collect(),
        total: img.parts.iter().map(|(_, d)| d.len()).sum::<usize>().max(1),
        index: 0,
        chunks: 1,
        report,
    };
    flasher
        .write_bins_to_flash(&segments, &mut progress)
        .map_err(|e| format!("writing failed: {e}"))?;
    flasher
        .connection()
        .reset_after(true, Chip::Esp32)
        .map_err(|e| format!("reset failed: {e}"))
}

// espflash wants segment lengths in whole 4-byte words.
fn padded(d: &[u8]) -> Vec<u8> {
    let mut v = d.to_vec();
    while !v.len().is_multiple_of(4) {
        v.push(0xFF);
    }
    v
}
