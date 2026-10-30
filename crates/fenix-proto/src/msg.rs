//! Message types of the fenixOS link (`proto/spec.md`).

use crate::codec::{decode_frame, encode_frame, FrameError};
use crate::{MAGIC, PROTO_VERSION};

pub mod ty {
    pub const HELLO: u8 = 0x01;
    pub const HELLO_ACK: u8 = 0x02;
    pub const PING: u8 = 0x03;
    pub const PONG: u8 = 0x04;
    pub const LOG: u8 = 0x05;
    pub const ERROR: u8 = 0x06;
    pub const STATS: u8 = 0x07;
    pub const TIME_SET: u8 = 0x10;
    pub const REBOOT: u8 = 0x11;
    pub const BOARD_STATE: u8 = 0x12;
    pub const TOUCH: u8 = 0x13;
    pub const SYSINFO_REQ: u8 = 0x14;
    pub const SYSINFO: u8 = 0x15;
    pub const RADIO_STATE: u8 = 0x16;
    pub const RADIO_SET: u8 = 0x17;
    pub const WIFI_SCAN_REQ: u8 = 0x18;
    pub const WIFI_SCAN: u8 = 0x19;
    pub const WIFI_JOIN: u8 = 0x1A;
    pub const APP_LIST_REQ: u8 = 0x20;
    pub const APP_LIST: u8 = 0x21;
    pub const APP_INSTALL_BEGIN: u8 = 0x22;
    pub const APP_CHUNK: u8 = 0x23;
    pub const APP_ACK: u8 = 0x24;
    pub const APP_INSTALL_END: u8 = 0x25;
    pub const APP_RESULT: u8 = 0x26;
    pub const APP_REMOVE: u8 = 0x27;
    pub const APP_OPENED: u8 = 0x28;
    pub const APP_CLOSED: u8 = 0x29;
    pub const HOST_STATE: u8 = 0x2A;
    pub const BIND_SET: u8 = 0x30;
    pub const EVENT: u8 = 0x31;
    pub const STREAM_START: u8 = 0x40;
    pub const STREAM_STOP: u8 = 0x41;
    pub const STREAM_RECT: u8 = 0x42;
    pub const STREAM_SYNC: u8 = 0x43;
    pub const STREAM_INFO: u8 = 0x44;
}

/// `ERROR` codes.
pub mod err {
    pub const PROTO_MISMATCH: u8 = 1;
    pub const UNKNOWN_TYPE: u8 = 2;
    pub const BAD_BODY: u8 = 3;
    pub const BUSY: u8 = 4;
    /// PING before HELLO: the board restarted and the PC must handshake again.
    pub const NO_SESSION: u8 = 5;
}

/// `APP_RESULT` ops and codes.
pub mod app {
    pub const OP_INSTALL: u8 = 1;
    pub const OP_REMOVE: u8 = 2;
    pub const OK: u8 = 0;
    pub const NO_SPACE: u8 = 1;
    pub const BAD_HASH: u8 = 2;
    pub const BAD_PACKAGE: u8 = 3;
    pub const BUSY: u8 = 4;
    pub const STORAGE: u8 = 5;
    pub const UNKNOWN_APP: u8 = 6;
}

/// `BOARD_STATE` screens.
pub mod screen {
    pub const BOOT: u8 = 0;
    pub const HOME: u8 = 1;
    pub const SETTINGS: u8 = 2;
    pub const SESSION: u8 = 3;
    pub const STREAM: u8 = 4;
    pub const APP: u8 = 5;
}

/// `EVENT` kinds.
pub mod event {
    pub const TAP: u8 = 1;
    pub const LONG: u8 = 2;
    pub const CHANGE: u8 = 3;
}

/// Stream codecs.
pub mod codec_id {
    pub const RAW: u8 = 0;
    pub const RLE16: u8 = 1;
}

#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct HelloAck {
    pub proto: u8,
    pub fw: (u8, u8, u8),
    pub width: u16,
    pub height: u16,
    pub rotation: u8,
    pub free_heap: u32,
    pub apps_used_kb: u16,
    pub apps_total_kb: u16,
    pub reset_reason: u8,
    pub uptime_s: u32,
    pub time_valid: bool,
    pub mac: [u8; 6],
}

#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Stats {
    pub free_heap: u32,
    pub min_heap: u32,
    pub fps_x10: u16,
    pub rx_frames: u32,
    pub rx_errors: u32,
    pub stream_dropped: u32,
}

/// `SYSINFO` flag bits.
pub mod sysflag {
    pub const DIMMED: u8 = 1;
    pub const RESTING: u8 = 2;
    pub const BURN_GUARD: u8 = 4;
    pub const STREAMING: u8 = 8;
    /// Wi-Fi is running (before P32: built in; no firmware set it).
    pub const WIFI: u8 = 16;
    /// Bluetooth is running (before P32: built in).
    pub const BLUETOOTH: u8 = 32;
}

/// Board load for the PC's Task manager. Counters are raw and cumulative (they wrap at 2^32):
/// the PC works out rates from two samples.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
#[cfg_attr(feature = "serde", derive(serde::Serialize))]
pub struct Sysinfo {
    /// The FreeRTOS run-time clock, µs; task run times count in the same unit.
    pub time_us: u32,
    pub cpu_mhz: u16,
    pub heap_total: u32,
    pub heap_free: u32,
    pub heap_min: u32,
    pub heap_largest: u32,
    pub dma_free: u32,
    pub flash_size: u32,
    pub app_size: u32,
    pub app_slot: u32,
    pub apps_used: u32,
    pub apps_total: u32,
    pub nvs_used: u16,
    pub nvs_total: u16,
    pub rx_bytes: u32,
    pub tx_bytes: u32,
    pub rx_frames: u32,
    pub rx_errors: u32,
    pub fps_x10: u16,
    pub backlight: u8,
    /// `sysflag` bits.
    pub flags: u8,
    pub tasks: Vec<TaskInfo>,
}

/// One FreeRTOS task in `SYSINFO`.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
#[cfg_attr(feature = "serde", derive(serde::Serialize))]
pub struct TaskInfo {
    /// FreeRTOS task number, unique for the board's uptime.
    pub id: u16,
    /// Core it is pinned to; 255 = either.
    pub core: u8,
    pub priority: u8,
    /// 0 running, 1 ready, 2 blocked, 3 suspended, 4 deleted.
    pub state: u8,
    /// Run time, µs on the `time_us` clock.
    pub runtime: u32,
    /// Least free stack since it started, bytes.
    pub stack_free: u16,
    pub name: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AppEntry {
    pub id: String,
    pub version: String,
    pub sha256: [u8; 32],
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum BindValue {
    Int(i32),
    Bool(bool),
    Text(String),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StreamRect {
    pub rect_id: u16,
    pub x: u16,
    pub y: u16,
    pub w: u16,
    pub h: u16,
    pub codec: u8,
    pub total: u32,
    pub offset: u32,
    pub data: Vec<u8>,
}

/// `RADIO_STATE` / `RADIO_SET` modes: one radio at a time, switched by a restart.
pub mod radio {
    pub const OFF: u8 = 0;
    pub const BLUETOOTH: u8 = 1;
    pub const WIFI: u8 = 2;
}

/// `RADIO_STATE` states.
pub mod radio_state {
    pub const OFF: u8 = 0;
    pub const STARTING: u8 = 1;
    /// Bluetooth: discoverable, nothing connected. Wi-Fi: no network set.
    pub const READY: u8 = 2;
    pub const CONNECTING: u8 = 3;
    pub const CONNECTED: u8 = 4;
    pub const FAILED: u8 = 5;
}

/// `WIFI_SCAN` security of an access point.
pub mod wifi_auth {
    pub const OPEN: u8 = 0;
    pub const WEP: u8 = 1;
    pub const WPA: u8 = 2;
    pub const WPA2: u8 = 3;
    pub const WPA_WPA2: u8 = 4;
    pub const ENTERPRISE: u8 = 5;
    pub const WPA3: u8 = 6;
    pub const WPA2_WPA3: u8 = 7;
    pub const OTHER: u8 = 8;
}

/// `WIFI_SCAN` status.
pub mod scan_status {
    pub const OK: u8 = 0;
    pub const NOT_WIFI: u8 = 1;
    pub const FAILED: u8 = 2;
}

/// The board's radio: which one runs and how it is doing.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
#[cfg_attr(feature = "serde", derive(serde::Serialize))]
pub struct RadioState {
    /// `radio` mode.
    pub mode: u8,
    /// `radio_state`.
    pub state: u8,
    /// Wi-Fi signal in dBm; 0 when unknown.
    pub rssi: i8,
    /// Wi-Fi IPv4 address, all zero when none.
    pub ip: [u8; 4],
    /// Wi-Fi network name, or the connected Bluetooth peer.
    pub name: String,
}

/// One access point in `WIFI_SCAN`.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
#[cfg_attr(feature = "serde", derive(serde::Serialize))]
pub struct WifiAp {
    pub ssid: String,
    pub rssi: i8,
    pub channel: u8,
    /// `wifi_auth`.
    pub auth: u8,
    pub bssid: [u8; 6],
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Msg {
    Hello {
        proto: u8,
    },
    HelloAck(HelloAck),
    Ping(u32),
    Pong(u32),
    Log {
        level: u8,
        text: String,
    },
    Error {
        code: u8,
        text: String,
    },
    Stats(Stats),
    SysinfoReq,
    Sysinfo(Sysinfo),
    RadioState(RadioState),
    /// Switch the radio: the board saves the mode and restarts.
    RadioSet(u8),
    WifiScanReq,
    WifiScan {
        status: u8,
        aps: Vec<WifiAp>,
    },
    /// Join a network (saved on the board in plaintext); an empty SSID forgets it.
    WifiJoin {
        ssid: String,
        password: String,
    },
    TimeSet(u32),
    Reboot,
    /// Remote touch from the PC (mirror clicks, tests), in current screen coordinates.
    Touch {
        x: u16,
        y: u16,
        down: bool,
    },
    BoardState {
        screen: u8,
        dimmed: bool,
        rotation: u8,
        app_id: String,
    },
    AppListReq,
    AppList(Vec<AppEntry>),
    AppInstallBegin {
        id: String,
        version: String,
        size: u32,
        sha256: [u8; 32],
    },
    AppChunk {
        offset: u32,
        data: Vec<u8>,
    },
    AppAck {
        next_offset: u32,
    },
    AppInstallEnd,
    AppResult {
        op: u8,
        code: u8,
        id: String,
        text: String,
    },
    AppRemove {
        id: String,
    },
    AppOpened {
        id: String,
    },
    AppClosed {
        id: String,
    },
    HostState {
        running: bool,
        id: String,
    },
    BindSet {
        bind: u16,
        id: String,
        value: BindValue,
    },
    Event {
        elem: u16,
        kind: u8,
        value: i32,
        id: String,
    },
    StreamStart {
        fps: u8,
    },
    StreamStop,
    StreamRect(StreamRect),
    StreamSync,
    StreamInfo {
        width: u16,
        height: u16,
        rotation: u8,
    },
}

struct W(Vec<u8>);

impl W {
    fn u8(&mut self, v: u8) -> &mut Self {
        self.0.push(v);
        self
    }
    fn u16(&mut self, v: u16) -> &mut Self {
        self.0.extend_from_slice(&v.to_le_bytes());
        self
    }
    fn u32(&mut self, v: u32) -> &mut Self {
        self.0.extend_from_slice(&v.to_le_bytes());
        self
    }
    fn i32(&mut self, v: i32) -> &mut Self {
        self.0.extend_from_slice(&v.to_le_bytes());
        self
    }
    fn bytes(&mut self, v: &[u8]) -> &mut Self {
        self.0.extend_from_slice(v);
        self
    }
    fn str(&mut self, s: &str) -> &mut Self {
        let b = &s.as_bytes()[..s.len().min(255)];
        self.u8(b.len() as u8).bytes(b)
    }
}

struct R<'a> {
    b: &'a [u8],
    i: usize,
}

impl<'a> R<'a> {
    fn take(&mut self, n: usize) -> Result<&'a [u8], FrameError> {
        if self.i + n > self.b.len() {
            return Err(FrameError::BadBody("short"));
        }
        let s = &self.b[self.i..self.i + n];
        self.i += n;
        Ok(s)
    }
    fn u8(&mut self) -> Result<u8, FrameError> {
        Ok(self.take(1)?[0])
    }
    fn u16(&mut self) -> Result<u16, FrameError> {
        Ok(u16::from_le_bytes(self.take(2)?.try_into().unwrap()))
    }
    fn u32(&mut self) -> Result<u32, FrameError> {
        Ok(u32::from_le_bytes(self.take(4)?.try_into().unwrap()))
    }
    fn i32(&mut self) -> Result<i32, FrameError> {
        Ok(i32::from_le_bytes(self.take(4)?.try_into().unwrap()))
    }
    fn arr<const N: usize>(&mut self) -> Result<[u8; N], FrameError> {
        Ok(self.take(N)?.try_into().unwrap())
    }
    fn str(&mut self) -> Result<String, FrameError> {
        let n = self.u8()? as usize;
        String::from_utf8(self.take(n)?.to_vec()).map_err(|_| FrameError::BadBody("utf8"))
    }
    fn rest(&mut self) -> &'a [u8] {
        let s = &self.b[self.i..];
        self.i = self.b.len();
        s
    }
    fn text(&mut self) -> String {
        String::from_utf8_lossy(self.rest()).into_owned()
    }
    fn end(&self) -> Result<(), FrameError> {
        if self.i == self.b.len() {
            Ok(())
        } else {
            Err(FrameError::BadBody("trailing bytes"))
        }
    }
}

impl Msg {
    pub fn hello() -> Self {
        Msg::Hello {
            proto: PROTO_VERSION,
        }
    }

    pub fn type_id(&self) -> u8 {
        use Msg::*;
        match self {
            Hello { .. } => ty::HELLO,
            HelloAck(_) => ty::HELLO_ACK,
            Ping(_) => ty::PING,
            Pong(_) => ty::PONG,
            Log { .. } => ty::LOG,
            Error { .. } => ty::ERROR,
            Stats(_) => ty::STATS,
            SysinfoReq => ty::SYSINFO_REQ,
            Sysinfo(_) => ty::SYSINFO,
            RadioState(_) => ty::RADIO_STATE,
            RadioSet(_) => ty::RADIO_SET,
            WifiScanReq => ty::WIFI_SCAN_REQ,
            WifiScan { .. } => ty::WIFI_SCAN,
            WifiJoin { .. } => ty::WIFI_JOIN,
            TimeSet(_) => ty::TIME_SET,
            Reboot => ty::REBOOT,
            Touch { .. } => ty::TOUCH,
            BoardState { .. } => ty::BOARD_STATE,
            AppListReq => ty::APP_LIST_REQ,
            AppList(_) => ty::APP_LIST,
            AppInstallBegin { .. } => ty::APP_INSTALL_BEGIN,
            AppChunk { .. } => ty::APP_CHUNK,
            AppAck { .. } => ty::APP_ACK,
            AppInstallEnd => ty::APP_INSTALL_END,
            AppResult { .. } => ty::APP_RESULT,
            AppRemove { .. } => ty::APP_REMOVE,
            AppOpened { .. } => ty::APP_OPENED,
            AppClosed { .. } => ty::APP_CLOSED,
            HostState { .. } => ty::HOST_STATE,
            BindSet { .. } => ty::BIND_SET,
            Event { .. } => ty::EVENT,
            StreamStart { .. } => ty::STREAM_START,
            StreamStop => ty::STREAM_STOP,
            StreamRect(_) => ty::STREAM_RECT,
            StreamSync => ty::STREAM_SYNC,
            StreamInfo { .. } => ty::STREAM_INFO,
        }
    }

    /// Payload = type ‖ seq ‖ body.
    pub fn encode(&self, seq: u8) -> Vec<u8> {
        use Msg::*;
        let mut w = W(Vec::with_capacity(32));
        w.u8(self.type_id()).u8(seq);
        match self {
            Hello { proto } => {
                w.bytes(&MAGIC).u8(*proto);
            }
            HelloAck(a) => {
                w.bytes(&MAGIC)
                    .u8(a.proto)
                    .u8(a.fw.0)
                    .u8(a.fw.1)
                    .u8(a.fw.2)
                    .u16(a.width)
                    .u16(a.height)
                    .u8(a.rotation);
                w.u32(a.free_heap)
                    .u16(a.apps_used_kb)
                    .u16(a.apps_total_kb)
                    .u8(a.reset_reason)
                    .u32(a.uptime_s);
                w.u8(a.time_valid as u8).bytes(&a.mac);
            }
            Ping(t) | Pong(t) => {
                w.u32(*t);
            }
            Log { level, text } => {
                w.u8(*level).bytes(text.as_bytes());
            }
            Error { code, text } => {
                w.u8(*code).bytes(text.as_bytes());
            }
            Stats(s) => {
                w.u32(s.free_heap)
                    .u32(s.min_heap)
                    .u16(s.fps_x10)
                    .u32(s.rx_frames)
                    .u32(s.rx_errors)
                    .u32(s.stream_dropped);
            }
            TimeSet(t) => {
                w.u32(*t);
            }
            Sysinfo(s) => {
                w.u32(s.time_us)
                    .u16(s.cpu_mhz)
                    .u32(s.heap_total)
                    .u32(s.heap_free)
                    .u32(s.heap_min)
                    .u32(s.heap_largest)
                    .u32(s.dma_free)
                    .u32(s.flash_size)
                    .u32(s.app_size)
                    .u32(s.app_slot)
                    .u32(s.apps_used)
                    .u32(s.apps_total)
                    .u16(s.nvs_used)
                    .u16(s.nvs_total)
                    .u32(s.rx_bytes)
                    .u32(s.tx_bytes)
                    .u32(s.rx_frames)
                    .u32(s.rx_errors)
                    .u16(s.fps_x10)
                    .u8(s.backlight)
                    .u8(s.flags)
                    .u8(s.tasks.len().min(255) as u8);
                for t in s.tasks.iter().take(255) {
                    w.u16(t.id)
                        .u8(t.core)
                        .u8(t.priority)
                        .u8(t.state)
                        .u32(t.runtime)
                        .u16(t.stack_free)
                        .str(&t.name);
                }
            }
            RadioState(s) => {
                w.u8(s.mode).u8(s.state).u8(s.rssi as u8).bytes(&s.ip).str(&s.name);
            }
            RadioSet(m) => {
                w.u8(*m);
            }
            WifiScan { status, aps } => {
                w.u8(*status).u8(aps.len() as u8);
                for a in aps {
                    w.str(&a.ssid).u8(a.rssi as u8).u8(a.channel).u8(a.auth).bytes(&a.bssid);
                }
            }
            WifiJoin { ssid, password } => {
                w.str(ssid).str(password);
            }
            Reboot | AppListReq | AppInstallEnd | StreamStop | StreamSync | SysinfoReq | WifiScanReq => {}
            Touch { x, y, down } => {
                w.u16(*x).u16(*y).u8(*down as u8);
            }
            BoardState {
                screen,
                dimmed,
                rotation,
                app_id,
            } => {
                w.u8(*screen).u8(*dimmed as u8).u8(*rotation).str(app_id);
            }
            AppList(list) => {
                w.u8(list.len() as u8);
                for e in list {
                    w.str(&e.id).str(&e.version).bytes(&e.sha256);
                }
            }
            AppInstallBegin {
                id,
                version,
                size,
                sha256,
            } => {
                w.str(id).str(version).u32(*size).bytes(sha256);
            }
            AppChunk { offset, data } => {
                w.u32(*offset).bytes(data);
            }
            AppAck { next_offset } => {
                w.u32(*next_offset);
            }
            AppResult { op, code, id, text } => {
                w.u8(*op).u8(*code).str(id).bytes(text.as_bytes());
            }
            AppRemove { id } | AppOpened { id } | AppClosed { id } => {
                w.str(id);
            }
            HostState { running, id } => {
                w.u8(*running as u8).str(id);
            }
            BindSet { bind, id, value } => {
                let kind = match value {
                    BindValue::Int(_) => 0,
                    BindValue::Bool(_) => 1,
                    BindValue::Text(_) => 2,
                };
                w.u16(*bind).u8(kind).str(id);
                match value {
                    BindValue::Int(v) => w.i32(*v),
                    BindValue::Bool(v) => w.u8(*v as u8),
                    BindValue::Text(v) => w.str(v),
                };
            }
            Event {
                elem,
                kind,
                value,
                id,
            } => {
                w.u16(*elem).u8(*kind).i32(*value).str(id);
            }
            StreamStart { fps } => {
                w.u8(*fps);
            }
            StreamRect(r) => {
                w.u16(r.rect_id)
                    .u16(r.x)
                    .u16(r.y)
                    .u16(r.w)
                    .u16(r.h)
                    .u8(r.codec)
                    .u32(r.total)
                    .u32(r.offset)
                    .bytes(&r.data);
            }
            StreamInfo {
                width,
                height,
                rotation,
            } => {
                w.u16(*width).u16(*height).u8(*rotation);
            }
        }
        w.0
    }

    /// Payload → (seq, message).
    pub fn decode(p: &[u8]) -> Result<(u8, Msg), FrameError> {
        if p.len() < 2 {
            return Err(FrameError::TooShort);
        }
        let (t, seq) = (p[0], p[1]);
        let mut r = R { b: &p[2..], i: 0 };
        let m = match t {
            ty::HELLO | ty::HELLO_ACK => {
                if r.arr::<4>()? != MAGIC {
                    return Err(FrameError::BadBody("magic"));
                }
                if t == ty::HELLO {
                    Msg::Hello { proto: r.u8()? }
                } else {
                    Msg::HelloAck(HelloAck {
                        proto: r.u8()?,
                        fw: (r.u8()?, r.u8()?, r.u8()?),
                        width: r.u16()?,
                        height: r.u16()?,
                        rotation: r.u8()?,
                        free_heap: r.u32()?,
                        apps_used_kb: r.u16()?,
                        apps_total_kb: r.u16()?,
                        reset_reason: r.u8()?,
                        uptime_s: r.u32()?,
                        time_valid: r.u8()? != 0,
                        mac: r.arr()?,
                    })
                }
            }
            ty::PING => Msg::Ping(r.u32()?),
            ty::PONG => Msg::Pong(r.u32()?),
            ty::LOG => Msg::Log {
                level: r.u8()?,
                text: r.text(),
            },
            ty::ERROR => Msg::Error {
                code: r.u8()?,
                text: r.text(),
            },
            ty::STATS => Msg::Stats(Stats {
                free_heap: r.u32()?,
                min_heap: r.u32()?,
                fps_x10: r.u16()?,
                rx_frames: r.u32()?,
                rx_errors: r.u32()?,
                stream_dropped: r.u32()?,
            }),
            ty::SYSINFO_REQ => Msg::SysinfoReq,
            ty::RADIO_STATE => Msg::RadioState(RadioState {
                mode: r.u8()?,
                state: r.u8()?,
                rssi: r.u8()? as i8,
                ip: r.arr()?,
                name: r.str()?,
            }),
            ty::RADIO_SET => Msg::RadioSet(r.u8()?),
            ty::WIFI_SCAN_REQ => Msg::WifiScanReq,
            ty::WIFI_SCAN => {
                let status = r.u8()?;
                let n = r.u8()?;
                let mut aps = Vec::with_capacity(n as usize);
                for _ in 0..n {
                    aps.push(WifiAp {
                        ssid: r.str()?,
                        rssi: r.u8()? as i8,
                        channel: r.u8()?,
                        auth: r.u8()?,
                        bssid: r.arr()?,
                    });
                }
                Msg::WifiScan { status, aps }
            }
            ty::WIFI_JOIN => Msg::WifiJoin {
                ssid: r.str()?,
                password: r.str()?,
            },
            ty::SYSINFO => {
                let mut s = Sysinfo {
                    time_us: r.u32()?,
                    cpu_mhz: r.u16()?,
                    heap_total: r.u32()?,
                    heap_free: r.u32()?,
                    heap_min: r.u32()?,
                    heap_largest: r.u32()?,
                    dma_free: r.u32()?,
                    flash_size: r.u32()?,
                    app_size: r.u32()?,
                    app_slot: r.u32()?,
                    apps_used: r.u32()?,
                    apps_total: r.u32()?,
                    nvs_used: r.u16()?,
                    nvs_total: r.u16()?,
                    rx_bytes: r.u32()?,
                    tx_bytes: r.u32()?,
                    rx_frames: r.u32()?,
                    rx_errors: r.u32()?,
                    fps_x10: r.u16()?,
                    backlight: r.u8()?,
                    flags: r.u8()?,
                    tasks: Vec::new(),
                };
                for _ in 0..r.u8()? {
                    s.tasks.push(TaskInfo {
                        id: r.u16()?,
                        core: r.u8()?,
                        priority: r.u8()?,
                        state: r.u8()?,
                        runtime: r.u32()?,
                        stack_free: r.u16()?,
                        name: r.str()?,
                    });
                }
                Msg::Sysinfo(s)
            }
            ty::TIME_SET => Msg::TimeSet(r.u32()?),
            ty::REBOOT => Msg::Reboot,
            ty::TOUCH => Msg::Touch {
                x: r.u16()?,
                y: r.u16()?,
                down: r.u8()? != 0,
            },
            ty::BOARD_STATE => Msg::BoardState {
                screen: r.u8()?,
                dimmed: r.u8()? != 0,
                rotation: r.u8()?,
                app_id: r.str()?,
            },
            ty::APP_LIST_REQ => Msg::AppListReq,
            ty::APP_LIST => {
                let n = r.u8()?;
                let mut v = Vec::with_capacity(n as usize);
                for _ in 0..n {
                    v.push(AppEntry {
                        id: r.str()?,
                        version: r.str()?,
                        sha256: r.arr()?,
                    });
                }
                Msg::AppList(v)
            }
            ty::APP_INSTALL_BEGIN => Msg::AppInstallBegin {
                id: r.str()?,
                version: r.str()?,
                size: r.u32()?,
                sha256: r.arr()?,
            },
            ty::APP_CHUNK => Msg::AppChunk {
                offset: r.u32()?,
                data: r.rest().to_vec(),
            },
            ty::APP_ACK => Msg::AppAck {
                next_offset: r.u32()?,
            },
            ty::APP_INSTALL_END => Msg::AppInstallEnd,
            ty::APP_RESULT => Msg::AppResult {
                op: r.u8()?,
                code: r.u8()?,
                id: r.str()?,
                text: r.text(),
            },
            ty::APP_REMOVE => Msg::AppRemove { id: r.str()? },
            ty::APP_OPENED => Msg::AppOpened { id: r.str()? },
            ty::APP_CLOSED => Msg::AppClosed { id: r.str()? },
            ty::HOST_STATE => Msg::HostState {
                running: r.u8()? != 0,
                id: r.str()?,
            },
            ty::BIND_SET => {
                let bind = r.u16()?;
                let kind = r.u8()?;
                let id = r.str()?;
                let value = match kind {
                    0 => BindValue::Int(r.i32()?),
                    1 => BindValue::Bool(r.u8()? != 0),
                    2 => BindValue::Text(r.str()?),
                    _ => return Err(FrameError::BadBody("bind kind")),
                };
                Msg::BindSet { bind, id, value }
            }
            ty::EVENT => Msg::Event {
                elem: r.u16()?,
                kind: r.u8()?,
                value: r.i32()?,
                id: r.str()?,
            },
            ty::STREAM_START => Msg::StreamStart { fps: r.u8()? },
            ty::STREAM_STOP => Msg::StreamStop,
            ty::STREAM_RECT => Msg::StreamRect(StreamRect {
                rect_id: r.u16()?,
                x: r.u16()?,
                y: r.u16()?,
                w: r.u16()?,
                h: r.u16()?,
                codec: r.u8()?,
                total: r.u32()?,
                offset: r.u32()?,
                data: r.rest().to_vec(),
            }),
            ty::STREAM_SYNC => Msg::StreamSync,
            ty::STREAM_INFO => Msg::StreamInfo {
                width: r.u16()?,
                height: r.u16()?,
                rotation: r.u8()?,
            },
            other => return Err(FrameError::UnknownType(other)),
        };
        r.end()?;
        Ok((seq, m))
    }

    /// Wire bytes of a whole frame, including the trailing 0x00.
    pub fn to_frame(&self, seq: u8) -> Vec<u8> {
        encode_frame(&self.encode(seq))
    }

    /// Wire bytes of one frame (without the 0x00) → (seq, message).
    pub fn from_frame(encoded: &[u8]) -> Result<(u8, Msg), FrameError> {
        Msg::decode(&decode_frame(encoded)?)
    }
}

/// RLE16 stream codec (`proto/spec.md`): `count:u8 ‖ pixel:u16be` runs.
pub fn rle16_decode(data: &[u8], out: &mut Vec<u16>) -> Result<(), FrameError> {
    let (runs, rest) = data.as_chunks::<3>();
    if !rest.is_empty() {
        return Err(FrameError::BadBody("rle16 length"));
    }
    for [n, hi, lo] in runs {
        out.extend(std::iter::repeat_n(
            u16::from_be_bytes([*hi, *lo]),
            *n as usize,
        ));
    }
    Ok(())
}

/// RLE16 encoder (the board has its own; this one is for tests and tools).
pub fn rle16_encode(px: &[u16]) -> Vec<u8> {
    let mut out = Vec::new();
    let mut i = 0;
    while i < px.len() {
        let p = px[i];
        let mut n = 1;
        while i + n < px.len() && px[i + n] == p && n < 255 {
            n += 1;
        }
        out.push(n as u8);
        out.extend_from_slice(&p.to_be_bytes());
        i += n;
    }
    out
}
