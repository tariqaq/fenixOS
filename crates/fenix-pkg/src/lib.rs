//! fenixOS app packages (`.fxp`): compiler (`build`) and reader (`parse`).
//! Format and limits: `docs/packages.md`. The board reader is `board/components/fenix_apps/app_pkg.cpp`.

use std::collections::HashMap;
use std::path::Path;

use serde::Deserialize;
use serde_json::{Map, Value};

pub const MAGIC: &[u8; 4] = b"FXP1";
pub const FORMAT: u8 = 1;
pub const HEADER_LEN: usize = 32;
pub const NONE: u16 = 0xFFFF;

// No cap on the package size since P51 (was 64 KB): the board reads packages in pieces (P39:
// strings, props and the shown images in RAM, never the whole file), so the content limits and
// the RAM estimate below are the real limits. The board's flash store takes ≤ 128 KB per package;
// the SD card takes any size.
pub const MAX_ELEMS: usize = 200;
pub const MAX_STRING_BYTES: usize = 8 * 1024;
pub const MAX_BINDS: usize = 64;
pub const MAX_IMAGE_BYTES: usize = 24 * 1024;
/// The RAM estimate's cap. Corrected (P50-M step 5, measured on the board): was 40 KB, which with
/// Wi-Fi on, the CJK font and the PC on Wi-Fi left about nothing at the lowest point.
pub const MAX_RAM: usize = 32 * 1024;
/// Per element built at once (LVGL objects and styles; measured ~150 B).
pub const RAM_PER_ELEM: usize = 160;
/// Per element in the package (its props and bookkeeping stay in RAM while the app is open).
pub const RAM_PER_PKG_ELEM: usize = 32;
pub const MAX_DEPTH: usize = 8;
/// Largest side of the bound image (album art, P50-M), in pixels. Its RGB565 buffer counts in
/// the RAM estimate. Measured: 120 x 120 left 0.9 KB at the lowest point (Wi-Fi on, CJK font).
pub const MAX_ART: i32 = 96;
/// App ids name files on the board (`/apps/<id>.fxp`); LittleFS names are ≤ 63 bytes.
pub const MAX_ID: usize = 48;

/// Letters, digits, `.`, `-`, `_`; 1 to `MAX_ID` bytes.
pub fn valid_id(id: &str) -> bool {
    !id.is_empty()
        && id.len() <= MAX_ID
        && id
            .bytes()
            .all(|c| c.is_ascii_alphanumeric() || b".-_".contains(&c))
}

pub const ICON_SMALL: u32 = 20;
pub const ICON_LARGE: u32 = 26;

/// Element kinds.
pub mod kind {
    pub const BOX: u8 = 1;
    pub const LABEL: u8 = 2;
    pub const BUTTON: u8 = 3;
    pub const SWITCH: u8 = 4;
    pub const SLIDER: u8 = 5;
    pub const BAR: u8 = 6;
    pub const IMAGE: u8 = 7;
    pub const ICON: u8 = 8;
    /// Shows one child, picked by an int binding (P50-M, fenixOS 0.1.25).
    pub const VIEW: u8 = 9;
}

/// First fenixOS that knows `view` and `show`: a package using them must say so in `min_fenixos`
/// (older boards would refuse kind 9 and ignore `show`).
pub const VIEW_SINCE: (u8, u8, u8) = (0, 1, 25);

/// Prop keys; `| PORTRAIT` marks a portrait-only value.
pub mod key {
    pub const TEXT: u8 = 1;
    pub const FONT: u8 = 2;
    pub const COLOR: u8 = 3;
    pub const BG: u8 = 4;
    pub const BIND: u8 = 5;
    pub const W: u8 = 6;
    pub const H: u8 = 7;
    pub const GROW: u8 = 8;
    pub const GAP: u8 = 9;
    pub const PAD: u8 = 10;
    pub const FLOW: u8 = 11;
    pub const MAIN: u8 = 12;
    pub const CROSS: u8 = 13;
    pub const RADIUS: u8 = 14;
    pub const MIN: u8 = 15;
    pub const MAX: u8 = 16;
    pub const VALUE: u8 = 17;
    pub const ICON: u8 = 18;
    pub const SIZE: u8 = 19;
    pub const SRC: u8 = 20;
    pub const EVENT: u8 = 21;
    pub const HIDDEN: u8 = 22;
    pub const ALIGN: u8 = 23;
    /// Bindable visibility: index of a bool binding (P50-M).
    pub const SHOW: u8 = 24;
    pub const PORTRAIT: u8 = 0x80;
}

pub mod asset_format {
    pub const A8: u8 = 1;
    pub const RGB565: u8 = 2;
}

pub const SIZE_CONTENT: i32 = -1;
pub const SIZE_PCT: i32 = 0x4000_0000;

pub const FONTS: [&str; 11] = [
    "body",
    "body_semibold",
    "label",
    "label_medium",
    "label_semibold",
    "caption",
    "caption_medium",
    "title",
    "title_l",
    "clock_56",
    "clock_64",
];
/// Built-in icons, in the board's order (`tools/gen_fonts.py` FILL_ICONS). The last 14 are new in
/// fenixOS 0.1.25 (P50-M): older boards refuse a package that names them. `fenix-sdk` has a copy.
pub const ICONS: [&str; 34] = [
    "gear-six",
    "power",
    "screencast",
    "arrow-clockwise",
    "plugs-connected",
    "plugs",
    "sun",
    "moon",
    "timer",
    "device-rotate",
    "hand-tap",
    "hard-drives",
    "desktop-tower",
    "info",
    "squares-four",
    "warning",
    "puzzle-piece",
    "check-circle",
    "x-circle",
    "monitor",
    "play",
    "pause",
    "skip-forward",
    "skip-back",
    "speaker-high",
    "speaker-x",
    "microphone",
    "microphone-slash",
    "headphones",
    "game-controller",
    "music-notes",
    "video",
    "phone",
    "sparkle",
];
/// Icons up to this index exist on every fenixOS; the rest need `VIEW_SINCE` (0.1.25).
pub const ICONS_BEFORE_0_1_25: usize = 20;
pub const ICON_SIZES: [i32; 9] = [12, 14, 15, 16, 18, 20, 26, 28, 40];

const TOKENS_JSON: &str = include_str!("../../../design/tokens.json");

#[derive(Debug, Clone, PartialEq)]
pub struct Elem {
    pub kind: u8,
    pub parent: u16,
    pub props: Vec<(u8, i32)>,
}

impl Elem {
    pub fn prop(&self, k: u8) -> Option<i32> {
        self.props
            .iter()
            .find(|(key, _)| *key == k)
            .map(|(_, v)| *v)
    }
}

/// The one `image` element with a `bind` (P50-M): the PC sets its pixels (`BIND_IMAGE`).
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Art {
    pub elem: u16,
    pub bind: u16,
    pub w: u16,
    pub h: u16,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Asset {
    pub format: u8,
    pub w: u16,
    pub h: u16,
    pub data: Vec<u8>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Package {
    pub min_fenixos: (u8, u8, u8),
    pub strings: Vec<String>,
    pub binds: Vec<u16>,
    pub elems: Vec<Elem>,
    pub assets: Vec<Asset>,
    pub id: u16,
    pub name: u16,
    pub version: u16,
    pub tile_color: u32,
    pub icon_small: u16,
    pub icon_large: u16,
}

impl Package {
    pub fn id(&self) -> &str {
        &self.strings[self.id as usize]
    }
    pub fn name(&self) -> &str {
        &self.strings[self.name as usize]
    }
    pub fn version(&self) -> &str {
        &self.strings[self.version as usize]
    }
    pub fn bind_names(&self) -> Vec<&str> {
        self.binds
            .iter()
            .map(|&s| self.strings[s as usize].as_str())
            .collect()
    }
    pub fn bind_id(&self, name: &str) -> Option<u16> {
        self.binds
            .iter()
            .position(|&s| self.strings[s as usize] == name)
            .map(|i| i as u16)
    }
    /// The bound image, if any (`check_limits` allows one, with a fixed size).
    pub fn art(&self) -> Option<Art> {
        self.elems.iter().enumerate().find_map(|(i, e)| {
            if e.kind != kind::IMAGE {
                return None;
            }
            Some(Art {
                elem: i as u16,
                bind: e.prop(key::BIND)? as u16,
                w: e.prop(key::W).unwrap_or(0) as u16,
                h: e.prop(key::H).unwrap_or(0) as u16,
            })
        })
    }

    /// Event name of element `elem` (what the PC half sees for an `EVENT`).
    pub fn event_name(&self, elem: u16) -> Option<&str> {
        let e = self.elems.get(elem as usize)?;
        e.prop(key::EVENT)
            .map(|s| self.strings[s as usize].as_str())
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut b = Vec::new();
        b.extend_from_slice(MAGIC);
        b.push(FORMAT);
        b.push(0);
        b.extend_from_slice(&[
            self.min_fenixos.0,
            self.min_fenixos.1,
            self.min_fenixos.2,
            0,
        ]);
        for v in [
            self.strings.len() as u16,
            self.binds.len() as u16,
            self.elems.len() as u16,
            self.assets.len() as u16,
            self.id,
            self.name,
            self.version,
        ] {
            b.extend_from_slice(&v.to_le_bytes());
        }
        b.extend_from_slice(&self.tile_color.to_le_bytes());
        b.extend_from_slice(&self.icon_small.to_le_bytes());
        b.extend_from_slice(&self.icon_large.to_le_bytes());
        debug_assert_eq!(b.len(), HEADER_LEN);
        for s in &self.strings {
            b.extend_from_slice(&(s.len() as u16).to_le_bytes());
            b.extend_from_slice(s.as_bytes());
        }
        for s in &self.binds {
            b.extend_from_slice(&s.to_le_bytes());
        }
        for e in &self.elems {
            b.push(e.kind);
            b.extend_from_slice(&e.parent.to_le_bytes());
            b.push(e.props.len() as u8);
            for (k, v) in &e.props {
                b.push(*k);
                b.extend_from_slice(&v.to_le_bytes());
            }
        }
        for a in &self.assets {
            b.push(a.format);
            b.extend_from_slice(&a.w.to_le_bytes());
            b.extend_from_slice(&a.h.to_le_bytes());
            b.extend_from_slice(&(a.data.len() as u32).to_le_bytes());
            b.extend_from_slice(&a.data);
        }
        b
    }

    /// Every index points at something, assets have the right size, the tree is ≤ `MAX_DEPTH` deep.
    pub fn check_refs(&self) -> Result<(), String> {
        let (ns, nb, na) = (self.strings.len(), self.binds.len(), self.assets.len());
        for (i, a) in self.assets.iter().enumerate() {
            let bpp = match a.format {
                asset_format::A8 => 1,
                asset_format::RGB565 => 2,
                f => return Err(format!("asset {i}: unknown format {f}")),
            };
            if a.data.len() != a.w as usize * a.h as usize * bpp {
                return Err(format!("asset {i}: size does not match {}x{}", a.w, a.h));
            }
        }
        for ix in [self.icon_small, self.icon_large] {
            if ix != NONE && ix as usize >= na {
                return Err("icon asset out of range".into());
            }
        }
        let mut depth = vec![0usize; self.elems.len()];
        for (i, e) in self.elems.iter().enumerate() {
            if e.kind < kind::BOX || e.kind > kind::VIEW {
                return Err(format!("element {i}: unknown kind {}", e.kind));
            }
            depth[i] = if e.parent == NONE {
                1
            } else {
                depth[e.parent as usize] + 1
            };
            if depth[i] > MAX_DEPTH {
                return Err(format!("tree deeper than {MAX_DEPTH}"));
            }
            for &(k, v) in &e.props {
                let limit = match k & !key::PORTRAIT {
                    key::TEXT | key::EVENT => ns,
                    key::BIND | key::SHOW => nb,
                    key::SRC => na,
                    key::ICON => ICONS.len(),
                    key::FONT => FONTS.len(),
                    _ => continue,
                };
                if v < 0 || v as usize >= limit {
                    return Err(format!("element {i}: prop {k} value {v} out of range"));
                }
            }
        }
        Ok(())
    }

    /// Elements the board builds at once: a `view` builds only the child it shows (P50-M), so
    /// it counts its largest child.
    pub fn built_at_once(&self) -> usize {
        let n = self.elems.len();
        let (mut sum, mut max, mut built) = (vec![0; n], vec![0; n], 0);
        for i in (0..n).rev() {
            let e = &self.elems[i];
            let own = 1 + if e.kind == kind::VIEW { max[i] } else { sum[i] };
            match e.parent {
                NONE => built += own,
                p => {
                    sum[p as usize] += own;
                    max[p as usize] = max[p as usize].max(own);
                }
            }
        }
        built
    }

    /// Check the limits of `docs/packages.md` (the board checks the same).
    pub fn check_limits(&self) -> Result<(), String> {
        let strings: usize = self.strings.iter().map(|s| s.len()).sum();
        // Every asset except the two tile icons counts as image data.
        let image_bytes: usize = self
            .assets
            .iter()
            .enumerate()
            .filter(|(i, _)| *i as u16 != self.icon_small && *i as u16 != self.icon_large)
            .map(|(_, a)| a.data.len())
            .sum();
        // The bound image (P50-M): one, a fixed size in pixels, the same in portrait; its buffer
        // is allocated when the app opens.
        let mut art_bytes = 0;
        let bound = |e: &&Elem| {
            e.kind == kind::IMAGE
                && e.props
                    .iter()
                    .any(|&(k, _)| k & !key::PORTRAIT == key::BIND)
        };
        for (n, e) in self.elems.iter().filter(bound).enumerate() {
            if n > 0 {
                return Err("only one image may have a bind".into());
            }
            let fixed = |k| {
                e.prop(k)
                    .filter(|v| (1..=MAX_ART).contains(v))
                    .ok_or_else(|| format!("a bound image needs w and h in pixels, 1 to {MAX_ART}"))
            };
            let (w, h) = (fixed(key::W)?, fixed(key::H)?);
            e.prop(key::BIND)
                .ok_or("a bound image cannot get its bind in portrait only")?;
            if e.props.iter().any(|&(k, _)| {
                k & key::PORTRAIT != 0
                    && [key::W, key::H, key::BIND].contains(&(k & !key::PORTRAIT))
            }) {
                return Err("a bound image cannot change size or bind in portrait".into());
            }
            art_bytes = (w * h * 2) as usize;
        }
        let ram = self.built_at_once() * RAM_PER_ELEM
            + self.elems.len() * RAM_PER_PKG_ELEM
            + strings
            + image_bytes
            + art_bytes;
        let checks = [
            (
                self.elems.len() <= MAX_ELEMS,
                format!("{} elements, limit {MAX_ELEMS}", self.elems.len()),
            ),
            (
                strings <= MAX_STRING_BYTES,
                format!("{strings} B of strings, limit {MAX_STRING_BYTES}"),
            ),
            (
                self.binds.len() <= MAX_BINDS,
                format!("{} bindings, limit {MAX_BINDS}", self.binds.len()),
            ),
            (
                image_bytes <= MAX_IMAGE_BYTES,
                format!("{image_bytes} B of images, limit {MAX_IMAGE_BYTES}"),
            ),
            (
                ram <= MAX_RAM,
                format!("RAM estimate {ram} B, limit {MAX_RAM}"),
            ),
        ];
        for (ok, msg) in checks {
            if !ok {
                return Err(msg);
            }
        }
        Ok(())
    }
}

struct Rd<'a> {
    b: &'a [u8],
    i: usize,
}

impl<'a> Rd<'a> {
    fn take(&mut self, n: usize) -> Result<&'a [u8], String> {
        let s = self.b.get(self.i..self.i + n).ok_or("truncated package")?;
        self.i += n;
        Ok(s)
    }
    fn u8(&mut self) -> Result<u8, String> {
        Ok(self.take(1)?[0])
    }
    fn u16(&mut self) -> Result<u16, String> {
        Ok(u16::from_le_bytes(self.take(2)?.try_into().unwrap()))
    }
    fn u32(&mut self) -> Result<u32, String> {
        Ok(u32::from_le_bytes(self.take(4)?.try_into().unwrap()))
    }
}

/// Read a package and check its structure and limits.
pub fn parse(bytes: &[u8]) -> Result<Package, String> {
    let mut r = Rd { b: bytes, i: 0 };
    if r.take(4)? != MAGIC {
        return Err("not a fenixOS package".into());
    }
    if r.u8()? != FORMAT {
        return Err("unknown package format".into());
    }
    r.u8()?;
    let min = (r.u8()?, r.u8()?, r.u8()?);
    r.u8()?;
    let (ns, nb, ne, na) = (r.u16()?, r.u16()?, r.u16()?, r.u16()?);
    let (id, name, version) = (r.u16()?, r.u16()?, r.u16()?);
    let tile_color = r.u32()?;
    let (icon_small, icon_large) = (r.u16()?, r.u16()?);
    let mut strings = Vec::with_capacity(ns as usize);
    for _ in 0..ns {
        let n = r.u16()? as usize;
        strings.push(String::from_utf8(r.take(n)?.to_vec()).map_err(|_| "bad utf-8")?);
    }
    let binds = (0..nb).map(|_| r.u16()).collect::<Result<Vec<_>, _>>()?;
    let mut elems = Vec::with_capacity(ne as usize);
    for i in 0..ne {
        let kind = r.u8()?;
        let parent = r.u16()?;
        if parent != NONE && parent >= i {
            return Err(format!("element {i}: parent {parent} not before it"));
        }
        let n = r.u8()?;
        let props = (0..n)
            .map(|_| Ok((r.u8()?, r.u32()? as i32)))
            .collect::<Result<Vec<_>, String>>()?;
        elems.push(Elem {
            kind,
            parent,
            props,
        });
    }
    let mut assets = Vec::with_capacity(na as usize);
    for _ in 0..na {
        let format = r.u8()?;
        let (w, h) = (r.u16()?, r.u16()?);
        let n = r.u32()? as usize;
        assets.push(Asset {
            format,
            w,
            h,
            data: r.take(n)?.to_vec(),
        });
    }
    if r.i != bytes.len() {
        return Err("trailing bytes".into());
    }
    let p = Package {
        min_fenixos: min,
        strings,
        binds,
        elems,
        assets,
        id,
        name,
        version,
        tile_color,
        icon_small,
        icon_large,
    };
    for s in [p.id, p.name, p.version]
        .into_iter()
        .chain(p.binds.iter().copied())
    {
        if s as usize >= p.strings.len() {
            return Err("string index out of range".into());
        }
    }
    if !valid_id(p.id()) {
        return Err("bad app id".into());
    }
    p.check_refs()?;
    p.check_limits()?;
    Ok(p)
}

// ---- Compiler ----

#[derive(Deserialize)]
struct Manifest {
    id: String,
    name: String,
    version: String,
    #[serde(default = "default_min")]
    min_fenixos: String,
    #[serde(default = "default_color")]
    color: String,
    icon: Option<String>,
}

fn default_min() -> String {
    "0.1.0".into()
}
fn default_color() -> String {
    "gray".into()
}

struct Compiler<'a> {
    dir: &'a Path,
    tokens: HashMap<String, u32>,
    strings: Vec<String>,
    string_ix: HashMap<String, u16>,
    binds: Vec<u16>,
    elems: Vec<Elem>,
    assets: Vec<Asset>,
}

fn parse_version(v: &str) -> Result<(u8, u8, u8), String> {
    let p: Vec<u8> = v
        .split('.')
        .map(|x| x.parse().map_err(|_| format!("bad version {v}")))
        .collect::<Result<_, _>>()?;
    match p.as_slice() {
        [a, b, c] => Ok((*a, *b, *c)),
        _ => Err(format!("bad version {v}")),
    }
}

fn hex_color(s: &str) -> Option<u32> {
    let h = s.strip_prefix('#')?;
    (h.len() == 6)
        .then(|| u32::from_str_radix(h, 16).ok())
        .flatten()
}

impl<'a> Compiler<'a> {
    fn new(dir: &'a Path) -> Result<Self, String> {
        let t: Value = serde_json::from_str(TOKENS_JSON).map_err(|e| e.to_string())?;
        let mut tokens = HashMap::new();
        if let Some(c) = t.get("color").and_then(Value::as_object) {
            for (k, v) in c {
                if let Some(rgb) = v.as_str().and_then(hex_color) {
                    tokens.insert(k.replace('-', "_"), rgb);
                }
            }
        }
        Ok(Self {
            dir,
            tokens,
            strings: Vec::new(),
            string_ix: HashMap::new(),
            binds: Vec::new(),
            elems: Vec::new(),
            assets: Vec::new(),
        })
    }

    fn string(&mut self, s: &str) -> u16 {
        if let Some(&i) = self.string_ix.get(s) {
            return i;
        }
        let i = self.strings.len() as u16;
        self.strings.push(s.to_string());
        self.string_ix.insert(s.to_string(), i);
        i
    }

    fn color(&self, s: &str) -> Result<u32, String> {
        hex_color(s)
            .or_else(|| self.tokens.get(&s.replace('-', "_")).copied())
            .or_else(|| self.tokens.get(&format!("tile_{s}")).copied())
            .ok_or_else(|| format!("unknown colour {s}"))
    }

    fn bind(&mut self, name: &str) -> i32 {
        let s = self.string(name);
        match self.binds.iter().position(|&b| b == s) {
            Some(i) => i as i32,
            None => {
                self.binds.push(s);
                (self.binds.len() - 1) as i32
            }
        }
    }

    fn load_png(&self, file: &str) -> Result<(u32, u32, Vec<u8>), String> {
        let path = self.dir.join(file);
        let f = std::fs::File::open(&path).map_err(|e| format!("{}: {e}", path.display()))?;
        let mut d = png::Decoder::new(f);
        d.set_transformations(png::Transformations::normalize_to_color8());
        let mut r = d.read_info().map_err(|e| format!("{file}: {e}"))?;
        let mut buf = vec![0; r.output_buffer_size()];
        let info = r.next_frame(&mut buf).map_err(|e| format!("{file}: {e}"))?;
        let (w, h) = (info.width, info.height);
        let mut rgba = Vec::with_capacity((w * h * 4) as usize);
        for px in buf[..info.buffer_size()].chunks(info.color_type.samples()) {
            let (r, g, b, a) = match px.len() {
                1 => (px[0], px[0], px[0], 255),
                2 => (px[0], px[0], px[0], px[1]),
                3 => (px[0], px[1], px[2], 255),
                _ => (px[0], px[1], px[2], px[3]),
            };
            rgba.extend_from_slice(&[r, g, b, a]);
        }
        Ok((w, h, rgba))
    }

    /// Alpha channel, area-averaged down (or nearest up) to `size` × `size`.
    fn a8_scaled(rgba: &[u8], w: u32, h: u32, size: u32) -> Vec<u8> {
        let mut out = Vec::with_capacity((size * size) as usize);
        for y in 0..size {
            for x in 0..size {
                let (x0, x1) = (x * w / size, ((x + 1) * w / size).max(x * w / size + 1));
                let (y0, y1) = (y * h / size, ((y + 1) * h / size).max(y * h / size + 1));
                let (mut sum, mut n) = (0u32, 0u32);
                for yy in y0..y1.min(h) {
                    for xx in x0..x1.min(w) {
                        sum += rgba[((yy * w + xx) * 4 + 3) as usize] as u32;
                        n += 1;
                    }
                }
                out.push((sum / n.max(1)) as u8);
            }
        }
        out
    }

    fn push_asset(&mut self, a: Asset) -> i32 {
        self.assets.push(a);
        (self.assets.len() - 1) as i32
    }

    fn image_asset(&mut self, file: &str, a8: bool) -> Result<i32, String> {
        let (w, h, rgba) = self.load_png(file)?;
        let data = if a8 {
            rgba.chunks(4).map(|p| p[3]).collect()
        } else {
            // RGB565 little-endian (LVGL's native order) over black (the board's screen background).
            let mut d = Vec::with_capacity((w * h * 2) as usize);
            for p in rgba.chunks(4) {
                let a = p[3] as u32;
                let (r, g, b) = (
                    p[0] as u32 * a / 255,
                    p[1] as u32 * a / 255,
                    p[2] as u32 * a / 255,
                );
                let v = ((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)) as u16;
                d.extend_from_slice(&v.to_le_bytes());
            }
            d
        };
        let format = if a8 {
            asset_format::A8
        } else {
            asset_format::RGB565
        };
        Ok(self.push_asset(Asset {
            format,
            w: w as u16,
            h: h as u16,
            data,
        }))
    }

    fn size(v: &Value) -> Result<i32, String> {
        match v {
            Value::Number(n) => Ok(n.as_i64().ok_or("size must be an integer")? as i32),
            Value::String(s) if s == "content" => Ok(SIZE_CONTENT),
            Value::String(s) if s.ends_with('%') => {
                let p: i32 = s
                    .trim_end_matches('%')
                    .parse()
                    .map_err(|_| format!("bad size {s}"))?;
                Ok(SIZE_PCT | p.clamp(0, 100))
            }
            _ => Err(format!("bad size {v}")),
        }
    }

    fn pick(list: &[&str], v: &str, what: &str) -> Result<i32, String> {
        list.iter()
            .position(|x| *x == v)
            .map(|i| i as i32)
            .ok_or_else(|| format!("unknown {what} {v}"))
    }

    fn props(
        &mut self,
        kind: u8,
        o: &Map<String, Value>,
        portrait: bool,
    ) -> Result<Vec<(u8, i32)>, String> {
        let mut out = Vec::new();
        let flag = if portrait { key::PORTRAIT } else { 0 };
        let str_of = |v: &Value, k: &str| {
            v.as_str()
                .map(str::to_string)
                .ok_or_else(|| format!("{k} must be a string"))
        };
        let int_of = |v: &Value, k: &str| {
            v.as_i64()
                .map(|x| x as i32)
                .ok_or_else(|| format!("{k} must be an integer"))
        };
        for (k, v) in o {
            let kv = match k.as_str() {
                "type" | "children" | "portrait" => continue,
                "text" => {
                    let s = str_of(v, k)?;
                    (key::TEXT, self.string(&s) as i32)
                }
                "font" => (key::FONT, Self::pick(&FONTS, &str_of(v, k)?, "font")?),
                "color" => (key::COLOR, self.color(&str_of(v, k)?)? as i32),
                "bg" => (key::BG, self.color(&str_of(v, k)?)? as i32),
                "bind" => (key::BIND, self.bind(&str_of(v, k)?)),
                "show" => (key::SHOW, self.bind(&str_of(v, k)?)),
                "w" => (key::W, Self::size(v)?),
                "h" => (key::H, Self::size(v)?),
                "grow" => (key::GROW, int_of(v, k)?),
                "gap" => (key::GAP, int_of(v, k)?),
                "pad" => (key::PAD, int_of(v, k)?),
                "radius" => (key::RADIUS, int_of(v, k)?),
                "min" => (key::MIN, int_of(v, k)?),
                "max" => (key::MAX, int_of(v, k)?),
                "value" => (
                    key::VALUE,
                    v.as_bool()
                        .map(i32::from)
                        .map_or_else(|| int_of(v, k), Ok)?,
                ),
                "flow" => (
                    key::FLOW,
                    Self::pick(&["column", "row", "row_wrap"], &str_of(v, k)?, "flow")?,
                ),
                "main" | "cross" => {
                    let a = Self::pick(
                        &["start", "center", "end", "space_between"],
                        &str_of(v, k)?,
                        "alignment",
                    )?;
                    (if k == "main" { key::MAIN } else { key::CROSS }, a)
                }
                "align" => (
                    key::ALIGN,
                    Self::pick(&["left", "center", "right"], &str_of(v, k)?, "align")?,
                ),
                "icon" => (key::ICON, Self::pick(&ICONS, &str_of(v, k)?, "icon")?),
                "size" => {
                    let s = int_of(v, k)?;
                    if !ICON_SIZES.contains(&s) {
                        return Err(format!("icon size {s} not built in ({ICON_SIZES:?})"));
                    }
                    (key::SIZE, s)
                }
                "src" => {
                    if portrait {
                        return Err("src cannot change in portrait".into());
                    }
                    let a8 = kind == kind::IMAGE && o.contains_key("color");
                    (key::SRC, self.image_asset(&str_of(v, k)?, a8)?)
                }
                "event" => {
                    let s = str_of(v, k)?;
                    (key::EVENT, self.string(&s) as i32)
                }
                "hidden" => (
                    key::HIDDEN,
                    v.as_bool().ok_or("hidden must be true or false")? as i32,
                ),
                other => return Err(format!("unknown prop {other}")),
            };
            out.push((kv.0 | flag, kv.1));
        }
        Ok(out)
    }

    fn element(&mut self, v: &Value, parent: u16, depth: usize) -> Result<(), String> {
        if depth > MAX_DEPTH {
            return Err(format!("tree deeper than {MAX_DEPTH}"));
        }
        let o = v.as_object().ok_or("element must be an object")?;
        let ty = o
            .get("type")
            .and_then(Value::as_str)
            .ok_or("element without type")?;
        let k = match ty {
            "box" => kind::BOX,
            "label" => kind::LABEL,
            "button" => kind::BUTTON,
            "switch" => kind::SWITCH,
            "slider" => kind::SLIDER,
            "bar" => kind::BAR,
            "image" => kind::IMAGE,
            "icon" => kind::ICON,
            "view" => kind::VIEW,
            other => return Err(format!("unknown element type {other}")),
        };
        let mut props = self.props(k, o, false)?;
        if let Some(p) = o.get("portrait") {
            let po = p.as_object().ok_or("portrait must be an object")?;
            props.extend(self.props(k, po, true)?);
        }
        let ix = self.elems.len();
        self.elems.push(Elem {
            kind: k,
            parent,
            props,
        });
        if let Some(children) = o.get("children") {
            if k != kind::BOX && k != kind::VIEW {
                return Err(format!("{ty} cannot have children"));
            }
            for c in children.as_array().ok_or("children must be a list")? {
                self.element(c, ix as u16, depth + 1)?;
            }
        }
        Ok(())
    }
}

/// Compile a package source folder (`app.toml`, `layout.json`, images) to `.fxp` bytes.
pub fn build(dir: &Path) -> Result<Vec<u8>, String> {
    let read = |f: &str| std::fs::read_to_string(dir.join(f)).map_err(|e| format!("{f}: {e}"));
    let m: Manifest = toml::from_str(&read("app.toml")?).map_err(|e| format!("app.toml: {e}"))?;
    let layout: Value =
        serde_json::from_str(&read("layout.json")?).map_err(|e| format!("layout.json: {e}"))?;
    if !valid_id(&m.id) {
        return Err(format!(
            "bad app id {:?} (letters, digits, . - _; ≤ {MAX_ID} bytes)",
            m.id
        ));
    }
    if m.name.is_empty() || m.name.len() > 24 {
        return Err("name must be 1–24 bytes".into());
    }
    parse_version(&m.version)?;
    let mut c = Compiler::new(dir)?;
    let id = c.string(&m.id);
    let name = c.string(&m.name);
    let version = c.string(&m.version);
    let tile_color = c.color(&m.color)?;
    let (mut icon_small, mut icon_large) = (NONE, NONE);
    if let Some(icon) = &m.icon {
        let (w, h, rgba) = c.load_png(icon)?;
        for (size, slot) in [(ICON_SMALL, &mut icon_small), (ICON_LARGE, &mut icon_large)] {
            let data = Compiler::a8_scaled(&rgba, w, h, size);
            *slot = c.push_asset(Asset {
                format: asset_format::A8,
                w: size as u16,
                h: size as u16,
                data,
            }) as u16;
        }
    }
    c.element(&layout, NONE, 1)?;
    let min_fenixos = parse_version(&m.min_fenixos)?;
    let new = c.elems.iter().any(|e| {
        e.kind == kind::VIEW
            || e.props.iter().any(|&(k, v)| {
                let k = k & !key::PORTRAIT;
                k == key::SHOW || (k == key::ICON && v as usize >= ICONS_BEFORE_0_1_25)
            })
    }) || c
        .elems
        .iter()
        .any(|e| e.kind == kind::IMAGE && e.prop(key::BIND).is_some());
    if new && min_fenixos < VIEW_SINCE {
        let (a, b, d) = VIEW_SINCE;
        return Err(format!(
            "view, show, a bound image and the newer icons need min_fenixos {a}.{b}.{d} or newer"
        ));
    }
    let p = Package {
        min_fenixos,
        strings: c.strings,
        binds: c.binds,
        elems: c.elems,
        assets: c.assets,
        id,
        name,
        version,
        tile_color,
        icon_small,
        icon_large,
    };
    let bytes = p.to_bytes();
    p.check_limits()?;
    Ok(bytes)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> Vec<u8> {
        let dir = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tools/sample-app/board");
        build(&dir).expect("sample app builds")
    }

    #[test]
    fn sample_round_trips() {
        let b = sample();
        let p = parse(&b).unwrap();
        assert_eq!(p.id(), "io.github.tariqaq.sample");
        assert_eq!(p.version(), "0.1.0");
        assert_eq!(p.to_bytes(), b);
        assert_eq!(p.bind_id("cpu"), Some(1));
        let hello = p.elems.iter().position(|e| e.kind == kind::BUTTON).unwrap();
        assert_eq!(p.event_name(hello as u16), Some("hello"));
    }

    /// Build a package folder in the temp dir with this layout and `min_fenixos`, then parse it.
    fn build_layout(name: &str, layout: &str, min: &str) -> Result<Package, String> {
        let dir = std::env::temp_dir().join(format!("fenix-pkg-test-{name}"));
        std::fs::create_dir_all(&dir).unwrap();
        let toml = format!(
            "id = \"t.{name}\"\nname = \"T\"\nversion = \"1.0.0\"\nmin_fenixos = \"{min}\"\ncolor = \"teal\"\n"
        );
        std::fs::write(dir.join("app.toml"), toml).unwrap();
        std::fs::write(dir.join("layout.json"), layout).unwrap();
        let built = build(&dir);
        let _ = std::fs::remove_dir_all(&dir);
        let b = built?;
        let p = parse(&b)?;
        assert_eq!(p.to_bytes(), b);
        Ok(p)
    }

    #[test]
    fn view_and_show() {
        let layout = r#"{ "type": "view", "bind": "mode", "value": 1, "children": [
            { "type": "label", "text": "A" },
            { "type": "box", "show": "extra", "children": [ { "type": "label", "text": "B" } ] }
        ] }"#;
        let p = build_layout("view", layout, "0.1.25").unwrap();
        assert_eq!(p.elems[0].kind, kind::VIEW);
        assert!(p.elems[0].props.contains(&(key::BIND, 0)));
        assert!(p.elems[2].props.contains(&(key::SHOW, 1)));
        assert_eq!(p.bind_id("extra"), Some(1));
        let old = build_layout("view-old", layout, "0.1.24").unwrap_err();
        assert!(old.contains("min_fenixos 0.1.25"), "{old}");
        let play = r#"{ "type": "button", "icon": "pause", "bind": "playing" }"#;
        assert!(build_layout("icon-old", play, "0.1.24").is_err());
        assert!(build_layout("icon-new", play, "0.1.25").is_ok());
        assert!(old.contains("min_fenixos 0.1.25"), "{old}");
        let leaf = r#"{ "type": "box", "children": [ { "type": "label", "show": "s", "children": [] } ] }"#;
        assert!(build_layout("view-leaf", leaf, "0.1.25").is_err());
    }

    #[test]
    fn bound_image() {
        let ok = r#"{ "type": "box", "children": [
            { "type": "image", "bind": "art", "w": 80, "h": 80 },
            { "type": "label", "text": "x", "bind": "title" } ] }"#;
        let p = build_layout("art", ok, "0.1.25").unwrap();
        let art = p.art().unwrap();
        assert_eq!((art.elem, art.bind, art.w, art.h), (1, 0, 80, 80));
        assert!(build_layout("art-old", ok, "0.1.24").is_err());
        let refused = [
            r#"{ "type": "image", "bind": "art", "w": 97, "h": 80 }"#,
            r#"{ "type": "image", "bind": "art", "w": "50%", "h": 80 }"#,
            r#"{ "type": "image", "bind": "art", "h": 80 }"#,
            r#"{ "type": "image", "bind": "art", "w": 80, "h": 80, "portrait": { "w": 60 } }"#,
            r#"{ "type": "box", "children": [
                { "type": "image", "bind": "a", "w": 8, "h": 8 },
                { "type": "image", "bind": "b", "w": 8, "h": 8 } ] }"#,
        ];
        for (n, l) in refused.iter().enumerate() {
            assert!(
                build_layout(&format!("art-bad{n}"), l, "0.1.25").is_err(),
                "{l}"
            );
        }
        // The buffer counts: 96 x 96 x 2 = 18.4 KB of the 32 KB estimate.
        let big = r#"{ "type": "image", "bind": "art", "w": 96, "h": 96 }"#;
        assert!(build_layout("art-big", big, "0.1.25").is_ok());
    }

    #[test]
    fn view_counts_its_largest_child() {
        // A view of three screens (3, 5 and 2 elements) built once: root + view + 5.
        let leaf = r#"{ "type": "label", "text": "x" }"#;
        let screen = |n: usize| {
            let kids = vec![leaf; n - 1].join(",");
            format!(r#"{{ "type": "box", "children": [{kids}] }}"#)
        };
        let layout = format!(
            r#"{{ "type": "box", "children": [ {{ "type": "view", "bind": "m", "children": [{}, {}, {}] }} ] }}"#,
            screen(3),
            screen(5),
            screen(2)
        );
        let p = build_layout("built", &layout, "0.1.25").unwrap();
        assert_eq!(p.elems.len(), 12);
        assert_eq!(p.built_at_once(), 7);
        assert_eq!(parse(&sample()).unwrap().built_at_once(), 15);
    }

    #[test]
    fn show_out_of_range_refused() {
        let mut p = parse(&sample()).unwrap();
        p.elems[1].props.push((key::SHOW, p.binds.len() as i32));
        assert!(parse(&p.to_bytes()).is_err());
        p.elems[1].props.pop();
        p.elems[1].kind = kind::VIEW + 1;
        assert!(parse(&p.to_bytes()).is_err());
    }

    #[test]
    fn rejects_bad_input() {
        let mut b = sample();
        assert!(parse(&b[..b.len() - 1]).is_err());
        b[0] = b'X';
        assert!(parse(&b).is_err());
    }
}
