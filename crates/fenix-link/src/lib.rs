//! Board link manager: finds the CH340 board, handshakes, keeps a heartbeat and
//! reconnects on unplug/reset. Plain std threads, no async runtime. The same session also runs
//! over Wi-Fi (P34): a port named `tcp:<host>[:<port>]` connects to the board's TCP link.
//! Ported from the FenixDeck PoC (single-thread serial I/O, see `docs/toolchain.md`).
//!
//! Usage: `let link = Link::spawn(LinkOptions::default());` then `link.send(msg)` and
//! read `link.events()`.

use std::io::{Read, Write};
use std::net::{TcpStream, ToSocketAddrs};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{self, Receiver, Sender};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

use fenix_proto::{err, FrameDecoder, HelloAck, Msg, BAUD, PROTO_VERSION, TCP_PORT, USB_PID, USB_VID};
use serialport::{SerialPort, SerialPortType};

#[derive(Debug, Clone)]
pub enum Event {
    /// Scanning; no board found yet.
    Searching,
    Connected {
        port: String,
        info: HelloAck,
    },
    Disconnected {
        reason: String,
    },
    /// Any message from the board (PONG excluded).
    Msg(Msg),
    /// Round-trip time of the last heartbeat.
    Rtt(Duration),
    /// A CH340 port that did not answer HELLO (blank board, other firmware, or a
    /// fenixOS with another protocol: `detail` says which). Sent once per port until it changes.
    Unresponsive {
        port: String,
        detail: String,
    },
    /// The link released the port (see `Link::pause`).
    Paused,
}

#[derive(Debug, Clone)]
pub struct LinkOptions {
    /// Force a port (e.g. "COM9", or "tcp:192.168.1.20" for Wi-Fi); None = auto-detect by USB
    /// VID:PID. `Link::set_port` changes it.
    pub port: Option<String>,
    /// Wi-Fi fallback (P34): a host (e.g. "fenixos.local") tried every `NET_RETRY` when no USB
    /// board port exists. None = USB only.
    pub net: Option<String>,
    pub baud: u32,
    pub heartbeat: Duration,
    pub timeout: Duration,
    pub rescan: Duration,
}

impl Default for LinkOptions {
    fn default() -> Self {
        Self {
            port: None,
            net: None,
            baud: BAUD,
            heartbeat: Duration::from_secs(1),
            timeout: Duration::from_secs(3),
            rescan: Duration::from_secs(1),
        }
    }
}

#[derive(Debug, Clone)]
pub struct PortInfo {
    pub name: String,
    pub vid: u16,
    pub pid: u16,
    pub product: Option<String>,
}

/// USB serial ports; `board` = matches the CH340 VID:PID.
pub fn list_ports() -> Vec<(PortInfo, bool)> {
    serialport::available_ports()
        .unwrap_or_default()
        .into_iter()
        .filter_map(|p| match p.port_type {
            SerialPortType::UsbPort(u) => Some((
                PortInfo {
                    name: p.port_name,
                    vid: u.vid,
                    pid: u.pid,
                    product: u.product,
                },
                u.vid == USB_VID && u.pid == USB_PID,
            )),
            _ => None,
        })
        .collect()
}

pub fn find_board_ports() -> Vec<String> {
    list_ports()
        .into_iter()
        .filter(|(_, b)| *b)
        .map(|(p, _)| p.name)
        .collect()
}

/// Opens a port without resetting the ESP32: DTR/RTS both released. Reads never wait: they
/// return what the driver has, or `TimedOut` at once (timeout 0 in the patched serialport, and
/// `no_wait_reads` as a second guard should the patch ever be dropped).
pub fn open_port(name: &str, baud: u32) -> serialport::Result<Box<dyn SerialPort>> {
    let mut p = serialport::new(name, baud)
        .timeout(Duration::ZERO)
        .dtr_on_open(false)
        .open_native()?;
    p.write_data_terminal_ready(false)?;
    p.write_request_to_send(false)?;
    no_wait_reads(&p)?;
    Ok(Box::new(p))
}

/// The WCH CH340 driver (CH341S64.SYS, 3.3.2011.11 and 4.0.2026.2) blue-screens the PC in the
/// code that arms a read timeout: it re-reads its current read request after another path has
/// completed it (bugchecks 0x48 and 0xD1, `docs/board-info.md`). serialport's timeouts
/// (interval and multiplier MAXDWORD, constant > 0: "wait for the first byte") take that path
/// on every read. MAXDWORD, 0, 0 makes the driver return at once without a timer, so the link
/// polls instead (`IDLE_POLL`). Writes get no timeout either: no flow control, so the UART
/// always drains.
#[cfg(windows)]
fn no_wait_reads(p: &serialport::COMPort) -> serialport::Result<()> {
    use std::os::windows::io::AsRawHandle;
    use windows_sys::Win32::Devices::Communication::{SetCommTimeouts, COMMTIMEOUTS};
    let t = COMMTIMEOUTS {
        ReadIntervalTimeout: u32::MAX,
        ReadTotalTimeoutMultiplier: 0,
        ReadTotalTimeoutConstant: 0,
        WriteTotalTimeoutMultiplier: 0,
        WriteTotalTimeoutConstant: 0,
    };
    if unsafe { SetCommTimeouts(p.as_raw_handle() as _, &t) } == 0 {
        return Err(std::io::Error::last_os_error().into());
    }
    Ok(())
}

#[cfg(not(windows))]
fn no_wait_reads(_: &serialport::TTYPort) -> serialport::Result<()> {
    Ok(())
}

/// A link to the board: a serial port or a TCP stream.
pub trait Conn: Read + Write + Send {}
impl<T: Read + Write + Send + ?Sized> Conn for T {}

/// The board over Wi-Fi. Reads behave like the serial port's: `TimedOut` when nothing has
/// arrived; a closed connection is an error, not an empty read.
struct Tcp(TcpStream);

impl Read for Tcp {
    fn read(&mut self, b: &mut [u8]) -> std::io::Result<usize> {
        use std::io::{Error, ErrorKind};
        match self.0.read(b) {
            Ok(0) if !b.is_empty() => Err(Error::new(ErrorKind::ConnectionAborted, "closed by the board")),
            Err(e) if e.kind() == ErrorKind::WouldBlock => Err(Error::new(ErrorKind::TimedOut, e)),
            r => r,
        }
    }
}

impl Write for Tcp {
    fn write(&mut self, b: &[u8]) -> std::io::Result<usize> {
        self.0.write(b)
    }
    fn flush(&mut self) -> std::io::Result<()> {
        self.0.flush()
    }
}

/// How often the Wi-Fi fallback is tried while no USB board port exists.
const NET_RETRY: Duration = Duration::from_secs(5);

/// `tcp:<host>[:<port>]` (port 7350 by default), IPv4.
fn open_tcp(spec: &str) -> std::io::Result<Tcp> {
    use std::io::{Error, ErrorKind};
    let host = spec.trim_start_matches("tcp:");
    let target = if host.contains(':') { host.to_string() } else { format!("{host}:{TCP_PORT}") };
    let addr = target
        .to_socket_addrs()?
        .find(|a| a.is_ipv4())
        .ok_or_else(|| Error::new(ErrorKind::NotFound, format!("{host}: no IPv4 address")))?;
    let s = TcpStream::connect_timeout(&addr, Duration::from_secs(2))?;
    s.set_nodelay(true)?;
    s.set_read_timeout(Some(Duration::from_millis(1)))?;
    s.set_write_timeout(Some(Duration::from_secs(2)))?;
    Ok(Tcp(s))
}

fn open_conn(name: &str, baud: u32) -> Result<Box<dyn Conn>, String> {
    if name.starts_with("tcp:") {
        return open_tcp(name).map(|t| Box::new(t) as Box<dyn Conn>).map_err(|e| e.to_string());
    }
    open_port(name, baud).map(|p| Box::new(p) as Box<dyn Conn>).map_err(|e| e.to_string())
}

/// Sleep between reads that found nothing: short while bytes are flowing (stream, install),
/// longer when the link is idle.
const IDLE_POLL_BUSY: Duration = Duration::from_millis(2);
const IDLE_POLL: Duration = Duration::from_millis(10);

pub struct Link {
    tx: Sender<Msg>,
    paused: Arc<AtomicBool>,
    port: Arc<Mutex<Option<String>>>,
    retarget: Arc<AtomicBool>,
    events: Mutex<Receiver<Event>>,
    stop: Arc<AtomicBool>,
    handle: Mutex<Option<thread::JoinHandle<()>>>,
}

impl Link {
    pub fn spawn(opts: LinkOptions) -> Self {
        let (tx, rx_out) = mpsc::channel::<Msg>();
        let (ev_tx, ev_rx) = mpsc::channel::<Event>();
        let stop = Arc::new(AtomicBool::new(false));
        let paused = Arc::new(AtomicBool::new(false));
        let port = Arc::new(Mutex::new(opts.port.clone()));
        let retarget = Arc::new(AtomicBool::new(false));
        let ctl = Ctl {
            stop: stop.clone(),
            paused: paused.clone(),
            port: port.clone(),
            retarget: retarget.clone(),
        };
        let handle = thread::Builder::new()
            .name("fenix-link".into())
            .spawn(move || manager(opts, rx_out, ev_tx, ctl))
            .expect("spawn link thread");
        Self {
            tx,
            paused,
            port,
            retarget,
            events: Mutex::new(ev_rx),
            stop,
            handle: Mutex::new(Some(handle)),
        }
    }

    /// Release the serial port (for flashing) or take it back. While paused the
    /// link neither scans nor holds a port; an `Event::Paused` confirms the release.
    pub fn pause(&self, on: bool) {
        self.paused.store(on, Ordering::Relaxed);
    }

    /// Use only this port from now on, or `None` to find the board on any CH340 port. A session on
    /// another port ends with the reason "port changed".
    pub fn set_port(&self, port: Option<String>) {
        if let Ok(mut p) = self.port.lock() {
            *p = port;
        }
        self.retarget.store(true, Ordering::Relaxed);
    }

    /// Queue a message for the board. Dropped while disconnected.
    pub fn send(&self, m: Msg) {
        let _ = self.tx.send(m);
    }

    pub fn sender(&self) -> Sender<Msg> {
        self.tx.clone()
    }

    /// Next event, waiting up to `timeout`.
    pub fn recv_event(&self, timeout: Duration) -> Option<Event> {
        self.events.lock().ok()?.recv_timeout(timeout).ok()
    }

    pub fn try_events(&self) -> Vec<Event> {
        match self.events.lock() {
            Ok(rx) => rx.try_iter().collect(),
            Err(_) => vec![],
        }
    }
}

impl Link {
    /// Stop the link thread and wait until it has closed the port. Call it before the process
    /// exits by a path that skips destructors (`std::process::exit`, Tauri's `app.exit`): a
    /// process that dies inside a serial `ReadFile` makes Windows cancel that read, and the CH340
    /// driver 3.3.2011.11 crashes the PC on that cancel (bugcheck 0x48, 2026-10-29).
    pub fn shutdown(&self) {
        self.stop.store(true, Ordering::Relaxed);
        let h = self.handle.lock().ok().and_then(|mut h| h.take());
        if let Some(h) = h {
            let _ = h.join();
        }
    }
}

impl Drop for Link {
    fn drop(&mut self) {
        self.shutdown();
    }
}

/// What the `Link` handle shares with its thread.
struct Ctl {
    stop: Arc<AtomicBool>,
    paused: Arc<AtomicBool>,
    port: Arc<Mutex<Option<String>>>,
    /// Set by `set_port`; the session checks it and the manager clears it.
    retarget: Arc<AtomicBool>,
}

fn manager(opts: LinkOptions, rx_out: Receiver<Msg>, ev: Sender<Event>, ctl: Ctl) {
    let (stop, paused) = (&*ctl.stop, &*ctl.paused);
    let mut announced_search = false;
    let mut announced_pause = false;
    let mut unresponsive: std::collections::HashMap<String, String> = Default::default();
    let mut next_net = Instant::now();
    while !stop.load(Ordering::Relaxed) {
        if paused.load(Ordering::Relaxed) {
            if !announced_pause {
                let _ = ev.send(Event::Paused);
                announced_pause = true;
            }
            while rx_out.try_recv().is_ok() {}
            sleep_unless_stopped(Duration::from_millis(100), stop);
            announced_search = false;
            continue;
        }
        announced_pause = false;
        ctl.retarget.store(false, Ordering::Relaxed);
        let fixed = ctl.port.lock().map(|p| p.clone()).unwrap_or(None);
        let candidates = match fixed.clone() {
            Some(p) => vec![p],
            None => {
                let mut c = find_board_ports();
                if let (true, Some(h)) = (c.is_empty(), &opts.net) {
                    if Instant::now() >= next_net {
                        next_net = Instant::now() + NET_RETRY;
                        c.push(format!("tcp:{h}"));
                    }
                }
                c
            }
        };
        let mut connected = false;
        for name in candidates {
            match handshake(&name, &opts) {
                Ok((port, dec, info, early)) => {
                    announced_search = false;
                    unresponsive.remove(&name);
                    let _ = ev.send(Event::Connected {
                        port: name.clone(),
                        info,
                    });
                    for m in early {
                        let _ = ev.send(Event::Msg(m));
                    }
                    let reason = session(port, dec, &opts, &rx_out, &ev, &ctl);
                    let _ = ev.send(Event::Disconnected { reason });
                    connected = true;
                    break;
                }
                // A Wi-Fi fallback that finds nothing is not a board that failed to answer.
                Err(_) if fixed.is_none() && name.starts_with("tcp:") => continue,
                Err(detail) => {
                    if unresponsive.get(&name) != Some(&detail) {
                        let _ = ev.send(Event::Unresponsive {
                            port: name.clone(),
                            detail: detail.clone(),
                        });
                        unresponsive.insert(name.clone(), detail);
                    }
                    continue;
                }
            }
        }
        if !connected {
            if !announced_search {
                let _ = ev.send(Event::Searching);
                announced_search = true;
            }
            // Drain queued messages so they don't pile up while offline.
            while rx_out.try_recv().is_ok() {}
            sleep_unless_stopped(opts.rescan, stop);
        }
    }
}

fn sleep_unless_stopped(d: Duration, stop: &AtomicBool) {
    let end = Instant::now() + d;
    while Instant::now() < end && !stop.load(Ordering::Relaxed) {
        thread::sleep(Duration::from_millis(50));
    }
}

type Handshake = (Box<dyn Conn>, FrameDecoder, HelloAck, Vec<Msg>);

fn handshake(name: &str, opts: &LinkOptions) -> Result<Handshake, String> {
    let mut port = open_conn(name, opts.baud)?;
    let mut dec = FrameDecoder::new();
    let mut buf = [0u8; 512];
    let deadline = Instant::now() + Duration::from_millis(2500);
    let mut seq = 0u8;
    let mut next_hello = Instant::now();
    let mut early = Vec::new();
    while Instant::now() < deadline {
        if Instant::now() >= next_hello {
            port.write_all(&Msg::hello().to_frame(seq))
                .map_err(|e| e.to_string())?;
            seq = seq.wrapping_add(1);
            next_hello = Instant::now() + Duration::from_millis(300);
        }
        let n = match port.read(&mut buf) {
            Ok(n) => n,
            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => 0,
            Err(e) => return Err(e.to_string()),
        };
        if n == 0 {
            thread::sleep(IDLE_POLL_BUSY);
        }
        if n > 0 && std::env::var("FENIX_DEBUG").is_ok() {
            eprintln!("hs {:02x?}", &buf[..n]);
        }
        let mut ack = None;
        dec.push(&buf[..n], |p| {
            if let Ok((_, m)) = Msg::decode(&p) {
                match m {
                    Msg::HelloAck(a) if ack.is_none() => ack = Some(Ok(a)),
                    Msg::Error { text, .. } if ack.is_none() => ack = Some(Err(text)),
                    // Frames right after the ack (e.g. boot log) are kept for the session.
                    other if matches!(ack, Some(Ok(_))) => early.push(other),
                    _ => {}
                }
            }
        });
        match ack {
            Some(Ok(a)) if a.proto == PROTO_VERSION => return Ok((port, dec, a, early)),
            Some(Ok(a)) => {
                return Err(format!(
                    "board protocol v{} != app v{}",
                    a.proto, PROTO_VERSION
                ))
            }
            Some(Err(t)) => return Err(t),
            None => {}
        }
    }
    Err("no HELLO_ACK".into())
}

fn session(
    mut port: Box<dyn Conn>,
    mut dec: FrameDecoder,
    opts: &LinkOptions,
    rx_out: &Receiver<Msg>,
    ev: &Sender<Event>,
    ctl: &Ctl,
) -> String {
    let (stop, paused) = (&*ctl.stop, &*ctl.paused);
    // One thread does both directions: batch-write everything pending, then read what
    // has arrived (reads never wait, see `no_wait_reads`), and sleep briefly when
    // nothing had.
    let start = Instant::now();
    let mut last_byte = Instant::now();
    let mut seq = 0u8;
    let mut last_rx = Instant::now();
    let mut next_ping = Instant::now();
    let mut pending_ping: Option<(u32, Instant)> = None;
    let mut out = Vec::with_capacity(4096);
    let mut buf = [0u8; 2048];
    loop {
        if stop.load(Ordering::Relaxed) {
            return "stopped".into();
        }
        if ctl.retarget.load(Ordering::Relaxed) {
            return "port changed".into();
        }
        if paused.load(Ordering::Relaxed) {
            return "paused".into();
        }
        out.clear();
        while let Ok(m) = rx_out.try_recv() {
            out.extend_from_slice(&m.to_frame(seq));
            seq = seq.wrapping_add(1);
        }
        if Instant::now() >= next_ping {
            let t = start.elapsed().as_millis() as u32;
            out.extend_from_slice(&Msg::Ping(t).to_frame(seq));
            seq = seq.wrapping_add(1);
            pending_ping = Some((t, Instant::now()));
            next_ping = Instant::now() + opts.heartbeat;
        }
        if !out.is_empty() {
            if let Err(e) = port.write_all(&out) {
                return format!("write: {e}");
            }
        }
        let n = match port.read(&mut buf) {
            Ok(n) => n,
            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => 0,
            Err(e) => return format!("read: {e}"),
        };
        if n > 0 {
            last_byte = Instant::now();
        } else if out.is_empty() {
            let busy = last_byte.elapsed() < Duration::from_millis(100);
            thread::sleep(if busy { IDLE_POLL_BUSY } else { IDLE_POLL });
        }
        let mut pongs = vec![];
        let mut restarted = false;
        dec.push(&buf[..n], |p| {
            if let Ok((_, m)) = Msg::decode(&p) {
                last_rx = Instant::now();
                match m {
                    Msg::Pong(t) => {
                        if let Some((pt, at)) = pending_ping {
                            if pt == t {
                                let _ = ev.send(Event::Rtt(at.elapsed()));
                                pending_ping = None;
                            }
                        }
                    }
                    Msg::Ping(t) => pongs.push(t),
                    Msg::Error {
                        code: err::NO_SESSION,
                        ..
                    } => restarted = true,
                    other => {
                        let _ = ev.send(Event::Msg(other));
                    }
                }
            }
        });
        for t in pongs {
            let _ = port.write_all(&Msg::Pong(t).to_frame(0));
        }
        if restarted {
            return "board restarted".into();
        }
        if last_rx.elapsed() > opts.timeout {
            return "heartbeat timeout".into();
        }
    }
}

/// PC copy of the board screen, rebuilt from `STREAM_INFO` and `STREAM_RECT` messages.
/// Pixels are RGB565 big-endian as on the LCD.
pub struct StreamCanvas {
    pub width: u16,
    pub height: u16,
    pub px: Vec<u16>,
    pending: Option<(fenix_proto::StreamRect, Vec<u8>)>,
}

/// A rectangle that became complete, in canvas coordinates.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Dirty {
    pub x: u16,
    pub y: u16,
    pub w: u16,
    pub h: u16,
}

impl Default for StreamCanvas {
    fn default() -> Self {
        Self::new(320, 240)
    }
}

impl StreamCanvas {
    pub fn new(width: u16, height: u16) -> Self {
        Self {
            width,
            height,
            px: vec![0; width as usize * height as usize],
            pending: None,
        }
    }

    pub fn resize(&mut self, width: u16, height: u16) {
        if (width, height) != (self.width, self.height) {
            *self = Self::new(width, height);
        }
    }

    /// Feed a stream message. Returns the rectangle that just became complete, if any.
    pub fn apply(&mut self, m: &Msg) -> Option<Dirty> {
        match m {
            Msg::StreamInfo { width, height, .. } => {
                self.resize(*width, *height);
                Some(Dirty {
                    x: 0,
                    y: 0,
                    w: *width,
                    h: *height,
                })
            }
            Msg::StreamRect(r) => {
                if r.offset == 0 {
                    self.pending = Some((r.clone(), Vec::with_capacity(r.total as usize)));
                    self.pending.as_mut()?.1.clear();
                }
                let (head, data) = self.pending.as_mut()?;
                if head.rect_id != r.rect_id || data.len() != r.offset as usize {
                    self.pending = None;
                    return None;
                }
                data.extend_from_slice(&r.data);
                if data.len() < head.total as usize {
                    return None;
                }
                let (head, data) = self.pending.take()?;
                self.blit(&head, &data)
            }
            _ => None,
        }
    }

    fn blit(&mut self, h: &fenix_proto::StreamRect, data: &[u8]) -> Option<Dirty> {
        let mut pix = Vec::with_capacity(h.w as usize * h.h as usize);
        match h.codec {
            fenix_proto::codec_id::RAW => pix.extend(
                data.as_chunks::<2>()
                    .0
                    .iter()
                    .map(|c| u16::from_be_bytes(*c)),
            ),
            fenix_proto::codec_id::RLE16 => fenix_proto::rle16_decode(data, &mut pix).ok()?,
            _ => return None,
        }
        if pix.len() != h.w as usize * h.h as usize
            || h.x + h.w > self.width
            || h.y + h.h > self.height
        {
            return None;
        }
        for row in 0..h.h as usize {
            let dst = (h.y as usize + row) * self.width as usize + h.x as usize;
            self.px[dst..dst + h.w as usize]
                .copy_from_slice(&pix[row * h.w as usize..(row + 1) * h.w as usize]);
        }
        Some(Dirty {
            x: h.x,
            y: h.y,
            w: h.w,
            h: h.h,
        })
    }

    /// RGBA8 copy of the whole canvas (for PNG export and the UI).
    pub fn to_rgba(&self) -> Vec<u8> {
        let mut out = Vec::with_capacity(self.px.len() * 4);
        for &p in &self.px {
            let (r, g, b) = ((p >> 11) & 0x1F, (p >> 5) & 0x3F, p & 0x1F);
            out.extend_from_slice(&[
                ((r * 527 + 23) >> 6) as u8,
                ((g * 259 + 33) >> 6) as u8,
                ((b * 527 + 23) >> 6) as u8,
                255,
            ]);
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fenix_proto::{codec_id, rle16_encode, StreamRect};

    #[test]
    fn canvas_applies_chunked_rle_rect() {
        let mut c = StreamCanvas::new(8, 4);
        let px: Vec<u16> = (0..12)
            .map(|i| if i < 6 { 0xF800 } else { 0x001F })
            .collect();
        let enc = rle16_encode(&px);
        let (a, b) = enc.split_at(3);
        let mk = |offset: u32, data: &[u8]| {
            Msg::StreamRect(StreamRect {
                rect_id: 1,
                x: 2,
                y: 1,
                w: 4,
                h: 3,
                codec: codec_id::RLE16,
                total: enc.len() as u32,
                offset,
                data: data.to_vec(),
            })
        };
        assert_eq!(c.apply(&mk(0, a)), None);
        assert_eq!(
            c.apply(&mk(3, b)),
            Some(Dirty {
                x: 2,
                y: 1,
                w: 4,
                h: 3
            })
        );
        assert_eq!(c.px[8 + 2], 0xF800);
        assert_eq!(c.px[3 * 8 + 5], 0x001F);
        assert_eq!(
            &c.to_rgba()[(8 + 2) * 4..(8 + 2) * 4 + 4],
            &[255, 0, 0, 255]
        );
    }
}
