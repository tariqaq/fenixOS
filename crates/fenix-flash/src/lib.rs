//! Board firmware flasher (PLAN §3.5), shared by the PC app and `fenix flash`: writes a bundled
//! image (`app/src-tauri/resources/firmware`, made by `tools/bundle_firmware.py`) with the
//! espflash library. espflash reads through the patched serialport (`third_party/serialport`),
//! whose reads never arm the CH340 driver's read timeout (P23). espflash resets the board into its
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
        let sha = hex(&Sha256::digest(&data));
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

/// An ESP-IDF build folder as `idf.py flash` would write it: the files and offsets in its
/// `flasher_args.json` (spikes, or the board build without bundling). The version is the
/// project's (`project_description.json`), or "build" when that is missing.
pub fn load_build(dir: &Path) -> Result<Image, String> {
    let args: serde_json::Value = std::fs::read(dir.join("flasher_args.json"))
        .map_err(|e| format!("no flasher_args.json ({e})"))
        .and_then(|b| serde_json::from_slice(&b).map_err(|e| format!("flasher_args.json: {e}")))?;
    let files = args["flash_files"]
        .as_object()
        .ok_or("flasher_args.json: no flash_files")?;
    let mut parts = Vec::new();
    for (off, rel) in files {
        let offset = u32::from_str_radix(off.trim_start_matches("0x"), 16)
            .map_err(|_| format!("flasher_args.json: bad offset {off}"))?;
        let rel = rel.as_str().ok_or("flasher_args.json: bad file")?;
        let data = std::fs::read(dir.join(rel)).map_err(|e| format!("{rel}: {e}"))?;
        parts.push((offset, data));
    }
    parts.sort_by_key(|(o, _)| *o);
    let version = std::fs::read(dir.join("project_description.json"))
        .ok()
        .and_then(|b| serde_json::from_slice::<serde_json::Value>(&b).ok())
        .and_then(|v| v["project_version"].as_str().map(str::to_string))
        .unwrap_or_else(|| "build".into());
    Ok(Image { version, parts })
}

/// One raw file written at `offset` (a full 4 MB backup goes at 0).
pub fn load_raw(offset: u32, file: &Path) -> Result<Image, String> {
    let data = std::fs::read(file).map_err(|e| format!("{}: {e}", file.display()))?;
    Ok(Image {
        version: "raw".into(),
        parts: vec![(offset, data)],
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

/// What the bootloader says about a board that does not answer as fenixOS (first-run probe).
#[derive(Debug, Clone, serde::Serialize)]
pub struct Probe {
    /// e.g. "esp32 rev 1.0".
    pub chip: String,
    /// e.g. "4MB".
    pub flash: String,
    pub mac: String,
}

/// Reset the board on `port` into its bootloader and connect (with the flasher stub).
fn connect(port: &str) -> Result<Flasher, String> {
    let info = serialport::available_ports()
        .map_err(|e| e.to_string())?
        .into_iter()
        .find(|p| p.port_name.eq_ignore_ascii_case(port))
        .and_then(|p| match p.port_type {
            serialport::SerialPortType::UsbPort(u) => Some(u),
            _ => None,
        })
        .ok_or_else(|| format!("{port} is not a USB serial port"))?;
    // The paused link lets go of the port only after its current handshake try (up to 2.5 s on a
    // board that does not answer), so keep trying for a while.
    let mut tries = 0;
    let serial = loop {
        match serialport::new(port, 115_200)
            .flow_control(serialport::FlowControl::None)
            .open_native()
        {
            Ok(s) => break s,
            Err(_) if tries < 40 => {
                tries += 1;
                std::thread::sleep(std::time::Duration::from_millis(100));
            }
            Err(e) => return Err(format!("{port}: {e}")),
        }
    };
    let conn = Connection::new(
        serial,
        info,
        ResetAfterOperation::HardReset,
        ResetBeforeOperation::DefaultReset,
        115_200,
    );
    Flasher::connect(conn, true, true, true, Some(Chip::Esp32), Some(FLASH_BAUD))
        .map_err(|e| format!("could not reach the bootloader: {e}"))
}

/// Ask the bootloader what chip is on `port`, then reset the board back into its own firmware.
/// Same rules as `flash`: off the UI thread, link paused.
pub fn probe(port: &str) -> Result<Probe, String> {
    let mut flasher = connect(port)?;
    let d = flasher.device_info().map_err(|e| e.to_string())?;
    let chip = match d.revision {
        Some((major, minor)) => format!("{} rev {major}.{minor}", d.chip),
        None => d.chip.to_string(),
    };
    let p = Probe {
        chip,
        flash: d.flash_size.to_string(),
        mac: d.mac_address.unwrap_or_default(),
    };
    flasher
        .connection()
        .reset_after(true, Chip::Esp32)
        .map_err(|e| format!("reset failed: {e}"))?;
    Ok(p)
}

/// Write the image to the board on `port`; `erase` wipes the whole flash first (first install:
/// whatever was on the board, its settings and files included, goes). Blocks for up to a minute;
/// call it off the UI and engine threads, with the board link paused (the port must be free).
pub fn flash(
    port: &str,
    img: &Image,
    erase: bool,
    report: &mut dyn FnMut(f32),
) -> Result<(), String> {
    let mut flasher = connect(port)?;
    if erase {
        flasher
            .erase_flash()
            .map_err(|e| format!("erasing failed: {e}"))?;
    }
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

fn hex(b: &[u8]) -> String {
    b.iter().map(|x| format!("{x:02x}")).collect()
}

/// Erase `size` bytes at `offset` (e.g. the settings partition), then reset the board.
pub fn erase(port: &str, offset: u32, size: u32) -> Result<(), String> {
    let mut flasher = connect(port)?;
    flasher
        .erase_region(offset, size)
        .map_err(|e| format!("erasing failed: {e}"))?;
    flasher
        .connection()
        .reset_after(true, Chip::Esp32)
        .map_err(|e| format!("reset failed: {e}"))
}

/// Read `size` bytes at `offset` into `out` (a backup: 0, 0x400000 for the whole 4 MB), then
/// reset the board.
pub fn read(port: &str, offset: u32, size: u32, out: &Path) -> Result<(), String> {
    let mut flasher = connect(port)?;
    // espflash's defaults: 4 KB blocks, 64 in flight.
    flasher
        .read_flash(offset, size, 0x1000, 64, out.to_path_buf())
        .map_err(|e| format!("reading failed: {e}"))?;
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
