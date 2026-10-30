//! `fenix`: developer CLI for the fenixOS board link.
//!
//! fenix ports                      list serial ports (CH340 marked)
//! fenix hello                      handshake and print the board info
//! fenix logs [secs]                print LOG / STATS / BOARD_STATE (default 30 s)
//! fenix time                       send TIME_SET (now, UTC)
//! fenix reboot                     ask the board to restart
//! fenix sysinfo                    the Task manager's numbers: two SYSINFO 1 s apart, per-core
//!                                  load, heap, flash, tasks with CPU % and stack free
//! fenix radio [off|wifi]           show the radio, or switch it (the board restarts)
//! fenix wifi scan                  networks the board hears (Wi-Fi mode)
//! fenix wifi join <ssid> [pass]    save a network on the board and connect; wifi forget
//! fenix weather <lat> <lon> <name>  weather place (Wi-Fi mode fetches it); weather off. The PC
//!                                  app sets its own place (or off) again when it connects
//! fenix tap <x> <y>                remote touch (screen coordinates)
//! fenix swipe <x1> <y1> <x2> <y2>  remote drag
//! fenix shot <out.png> [secs]      stream the screen for a while, save the last frame
//! fenix bench [secs] [nostream]    link throughput
//! fenix list                       installed apps
//! fenix install <file.fxp>         push a board package
//! fenix remove <id>                remove an app
//! fenix set <file.fxp> <bind>=<value>...   set bindings like an app's PC half
//! fenix host <id> on|off           tell the board an app's PC half runs or stopped
//! fenix soak <minutes>             ping + stats soak, counts errors and reconnects
//! fenix flash [dir] [--erase]      write firmware: a bundle (default app/src-tauri/resources/
//!                                  firmware, from tools/bundle_firmware.py) or an ESP-IDF build
//!                                  folder (flasher_args.json, e.g. a spike); quit the app first
//! fenix write <offset> <file>      write one raw file (a 4 MB backup goes at 0)
//! fenix read-flash <offset> <size> <file>   read flash to a file (backup: 0 0x400000)
//! fenix erase <offset> <size>      erase a flash region (0x9000 0x6000: settings)
//! The flash commands replace esptool and `idf.py flash`, whose read timeouts take the CH340
//! driver's crash path (P23).
//! Common option: --port COMx, or --port tcp:<host> for the Wi-Fi link (default: auto-detect USB).

use std::path::Path;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use fenix_link::{Event, Link, LinkOptions, StreamCanvas};
use fenix_proto::{app, radio, scan_status, BindValue, Msg, RadioState, Stats, Sysinfo};
use sha2::{Digest, Sha256};

fn main() {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    let mut opts = LinkOptions::default();
    if let Some(i) = args.iter().position(|a| a == "--port") {
        opts.port = args.get(i + 1).cloned();
        args.drain(i..(i + 2).min(args.len()));
    }
    let cmd = args.first().map(String::as_str).unwrap_or("help");
    let code = match cmd {
        "ports" => {
            for (p, board) in fenix_link::list_ports() {
                println!(
                    "{} {:04x}:{:04x} {}{}",
                    p.name,
                    p.vid,
                    p.pid,
                    p.product.unwrap_or_default(),
                    if board { "  <- board" } else { "" }
                );
            }
            0
        }
        "hello" => with_link(opts, |_| true, 5),
        "logs" => {
            let secs = arg_u64(&args, 1, 30);
            with_link(opts, |_| false, secs)
        }
        "time" => one_shot(opts, Msg::TimeSet(now_s())),
        "reboot" => one_shot(opts, Msg::Reboot),
        "sysinfo" => sysinfo(opts),
        "radio" => match args.get(1).map(|s| s.as_str()) {
            None => radio_show(opts),
            Some("off") => one_shot(opts, Msg::RadioSet(radio::OFF)),
            Some("wifi") => one_shot(opts, Msg::RadioSet(radio::WIFI)),
            _ => usage(),
        },
        "weather" => match (args.get(1).map(|s| s.as_str()), args.get(2), args.get(3)) {
            (Some("off"), _, _) => one_shot(
                opts,
                Msg::WeatherSet {
                    lat_e4: 0,
                    lon_e4: 0,
                    name: String::new(),
                },
            ),
            (Some(lat), Some(lon), Some(_)) => match (lat.parse::<f64>(), lon.parse::<f64>()) {
                (Ok(la), Ok(lo)) => one_shot(
                    opts,
                    Msg::WeatherSet {
                        lat_e4: (la * 1e4).round() as i32,
                        lon_e4: (lo * 1e4).round() as i32,
                        name: args[3..].join(" "),
                    },
                ),
                _ => usage(),
            },
            _ => usage(),
        },
        "wifi" => match (args.get(1).map(|s| s.as_str()), args.get(2)) {
            (Some("scan"), _) => wifi_scan(opts),
            (Some("join"), Some(ssid)) => one_shot(
                opts,
                Msg::WifiJoin {
                    ssid: ssid.clone(),
                    password: args.get(3).cloned().unwrap_or_default(),
                },
            ),
            (Some("forget"), _) => one_shot(
                opts,
                Msg::WifiJoin {
                    ssid: String::new(),
                    password: String::new(),
                },
            ),
            _ => usage(),
        },
        "swipe" => {
            let v: Vec<u16> = args[1..].iter().filter_map(|a| a.parse().ok()).collect();
            if v.len() == 4 {
                swipe(opts, (v[0], v[1]), (v[2], v[3]))
            } else {
                usage()
            }
        }
        "tap" => match (
            args.get(1).and_then(|v| v.parse().ok()),
            args.get(2).and_then(|v| v.parse().ok()),
        ) {
            (Some(x), Some(y)) => tap(opts, x, y),
            _ => usage(),
        },
        "bench" => bench(
            opts,
            arg_u64(&args, 1, 10),
            args.iter().any(|a| a == "nostream"),
        ),
        "shot" => shot(
            opts,
            args.get(1).map(String::as_str).unwrap_or("shot.png"),
            arg_u64(&args, 2, 3),
        ),
        "list" => list(opts),
        "install" => match args.get(1) {
            Some(f) => install(opts, Path::new(f)),
            None => usage(),
        },
        "remove" => match args.get(1) {
            Some(id) => remove(opts, id),
            None => usage(),
        },
        "set" if args.len() >= 3 => set(opts, Path::new(&args[1]), &args[2..]),
        "host" => match (args.get(1), args.get(2).map(String::as_str)) {
            (Some(id), Some("on" | "off")) => one_shot(
                opts,
                Msg::HostState {
                    running: args[2] == "on",
                    id: id.clone(),
                },
            ),
            _ => usage(),
        },
        "soak" => soak(opts, arg_u64(&args, 1, 60)),
        "flash" => flash(opts, &args[1..]),
        "write" => match (args.get(1).and_then(|v| num(v)), args.get(2)) {
            (Some(off), Some(f)) => match fenix_flash::load_raw(off, Path::new(f)) {
                Ok(img) => write_image(opts, &img, false),
                Err(e) => {
                    eprintln!("{e}");
                    1
                }
            },
            _ => usage(),
        },
        "read-flash" => match (
            args.get(1).and_then(|v| num(v)),
            args.get(2).and_then(|v| num(v)),
            args.get(3),
        ) {
            (Some(off), Some(size), Some(f)) => read_flash(opts, off, size, Path::new(f)),
            _ => usage(),
        },
        "erase" => match (
            args.get(1).and_then(|v| num(v)),
            args.get(2).and_then(|v| num(v)),
        ) {
            (Some(off), Some(size)) => erase(opts, off, size),
            _ => usage(),
        },
        _ => usage(),
    };
    std::process::exit(code);
}

fn usage() -> i32 {
    eprintln!("usage: fenix ports|hello|logs [s]|time|reboot|sysinfo|tap <x> <y>|swipe <x1> <y1> <x2> <y2>|shot <png> [s]|bench [s] [nostream]|list|install <file.fxp>|remove <id>|set <file.fxp> <bind>=<value>...|host <id> on|off|soak <min>|flash [dir] [--erase]|write <offset> <file>|read-flash <offset> <size> <file>|erase <offset> <size>|radio [off|wifi]|wifi scan|join <ssid> [pass]|forget|weather <lat> <lon> <name>|off [--port COMx|tcp:<host>]");
    2
}

/// Write the bundled firmware with the app's flasher (crash-safe reads, P23); replaces
/// `idf.ps1 flash`, whose esptool sets read timeouts on the CH340.
fn flash(opts: LinkOptions, args: &[String]) -> i32 {
    let erase = args.iter().any(|a| a == "--erase");
    let dir = args.iter().find(|a| !a.starts_with("--")).map_or_else(
        || Path::new("app/src-tauri/resources/firmware").to_path_buf(),
        Into::into,
    );
    let loaded = if dir.join("flasher_args.json").exists() {
        fenix_flash::load_build(&dir)
    } else {
        fenix_flash::load(&dir)
    };
    match loaded {
        Ok(img) => write_image(opts, &img, erase),
        Err(e) => {
            eprintln!("{}: {e}", dir.display());
            1
        }
    }
}

fn read_flash(opts: LinkOptions, offset: u32, size: u32, out: &Path) -> i32 {
    let Some(port) = board_port(&opts) else {
        eprintln!("no board found (use --port COMx)");
        return 1;
    };
    println!("reading {size:#x} bytes at {offset:#x} from {port}");
    match fenix_flash::read(&port, offset, size, out) {
        Ok(()) => {
            println!("saved {}; the board restarts", out.display());
            0
        }
        Err(e) => {
            eprintln!("{e}");
            1
        }
    }
}

fn write_image(opts: LinkOptions, img: &fenix_flash::Image, erase: bool) -> i32 {
    let Some(port) = board_port(&opts) else {
        eprintln!("no board found (use --port COMx)");
        return 1;
    };
    println!(
        "flashing v{} to {port}{}",
        img.version,
        if erase { " (erase first)" } else { "" }
    );
    let mut last = -1;
    let r = fenix_flash::flash(&port, img, erase, &mut |f| {
        let pct = (f * 100.0) as i32;
        if pct / 10 != last / 10 {
            println!("{pct:3} %");
        }
        last = pct;
    });
    match r {
        Ok(()) => {
            println!("done; the board restarts");
            0
        }
        Err(e) => {
            eprintln!("{e}");
            1
        }
    }
}

/// A number in decimal or 0x hex.
fn num(s: &str) -> Option<u32> {
    match s.strip_prefix("0x") {
        Some(h) => u32::from_str_radix(h, 16).ok(),
        None => s.parse().ok(),
    }
}

fn board_port(opts: &LinkOptions) -> Option<String> {
    opts.port
        .clone()
        .or_else(|| fenix_link::find_board_ports().into_iter().next())
}

fn erase(opts: LinkOptions, offset: u32, size: u32) -> i32 {
    let Some(port) = board_port(&opts) else {
        eprintln!("no board found (use --port COMx)");
        return 1;
    };
    match fenix_flash::erase(&port, offset, size) {
        Ok(()) => {
            println!("erased {size:#x} bytes at {offset:#x}; the board restarts");
            0
        }
        Err(e) => {
            eprintln!("{e}");
            1
        }
    }
}

fn arg_u64(args: &[String], i: usize, def: u64) -> u64 {
    args.get(i).and_then(|s| s.parse().ok()).unwrap_or(def)
}

fn now_s() -> u32 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs() as u32)
        .unwrap_or(0)
}

fn ts() -> String {
    let ms = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis())
        .unwrap_or(0);
    let s = (ms / 1000) % 86_400;
    format!(
        "{:02}:{:02}:{:02}.{:03}",
        (s / 3600 + 8) % 24,
        (s / 60) % 60,
        s % 60,
        ms % 1000
    )
}

/// Wait for a connection, then print events for `secs`. `stop_after` ends early.
fn with_link(opts: LinkOptions, stop_after: impl Fn(&Event) -> bool, secs: u64) -> i32 {
    let link = Link::spawn(opts);
    let end = Instant::now() + Duration::from_secs(secs.max(1)) + Duration::from_secs(5);
    let mut connected = false;
    while Instant::now() < end {
        if let Some(e) = link.recv_event(Duration::from_millis(200)) {
            print_event(&e);
            if matches!(e, Event::Connected { .. }) {
                connected = true;
            }
            if stop_after(&e) {
                return 0;
            }
        }
    }
    if connected {
        0
    } else {
        1
    }
}

fn print_event(e: &Event) {
    match e {
        Event::Connected { port, info } => println!(
            "{} connected {port}: fenixOS v{}.{}.{} proto {} {}x{} rot {} heap {} apps {}/{} KB reset {} up {} s time {} mac {}",
            ts(),
            info.fw.0,
            info.fw.1,
            info.fw.2,
            info.proto,
            info.width,
            info.height,
            info.rotation,
            info.free_heap,
            info.apps_used_kb,
            info.apps_total_kb,
            info.reset_reason,
            info.uptime_s,
            if info.time_valid { "valid" } else { "unset" },
            info.mac.iter().map(|b| format!("{b:02x}")).collect::<Vec<_>>().join(":")
        ),
        Event::Msg(Msg::Log { level, text }) => println!("{} log[{level}] {text}", ts()),
        Event::Msg(Msg::StreamRect(_)) => {}
        Event::Msg(m) => println!("{} {m:?}", ts()),
        Event::Rtt(_) => {}
        other => println!("{} {other:?}", ts()),
    }
}

/// Connect, send one message, wait a moment so it goes out.
fn one_shot(opts: LinkOptions, m: Msg) -> i32 {
    let link = Link::spawn(opts);
    if !wait_connected(&link, 6) {
        eprintln!("no board");
        return 1;
    }
    link.send(m);
    std::thread::sleep(Duration::from_millis(300));
    0
}

fn ask_sysinfo(link: &Link) -> Option<Sysinfo> {
    link.send(Msg::SysinfoReq);
    let end = Instant::now() + Duration::from_secs(2);
    while Instant::now() < end {
        match link.recv_event(Duration::from_millis(50)) {
            Some(Event::Msg(Msg::Sysinfo(s))) => return Some(s),
            Some(Event::Msg(Msg::Error { code, text })) => {
                eprintln!("board error {code}: {text} (firmware without SYSINFO?)");
                return None;
            }
            _ => {}
        }
    }
    eprintln!("no SYSINFO answer");
    None
}

fn radio_text(s: &RadioState) -> String {
    let mode = ["off", "Bluetooth (old firmware)", "Wi-Fi"].get(s.mode as usize).copied().unwrap_or("?");
    let state = ["off", "starting", "ready", "connecting", "connected", "failed"]
        .get(s.state as usize)
        .copied()
        .unwrap_or("?");
    let mut t = format!("radio: {mode}, {state}");
    if !s.name.is_empty() {
        t += &format!(", {}", s.name);
    }
    if s.ip != [0; 4] {
        let [a, b, c, d] = s.ip;
        t += &format!(", {a}.{b}.{c}.{d}, {} dBm", s.rssi);
    }
    t
}

/// The board sends RADIO_STATE right after the handshake (firmware 0.1.8 and later).
fn radio_show(opts: LinkOptions) -> i32 {
    let link = Link::spawn(opts);
    let end = Instant::now() + Duration::from_secs(6);
    while Instant::now() < end {
        if let Some(Event::Msg(Msg::RadioState(s))) = link.recv_event(Duration::from_millis(50)) {
            println!("{}", radio_text(&s));
            return 0;
        }
    }
    eprintln!("no RADIO_STATE (firmware before 0.1.8?)");
    1
}

fn wifi_scan(opts: LinkOptions) -> i32 {
    let link = Link::spawn(opts);
    if !wait_connected(&link, 6) {
        eprintln!("no board");
        return 1;
    }
    link.send(Msg::WifiScanReq);
    let end = Instant::now() + Duration::from_secs(10);
    while Instant::now() < end {
        match link.recv_event(Duration::from_millis(50)) {
            Some(Event::Msg(Msg::WifiScan { status, aps })) => {
                if status == scan_status::NOT_WIFI {
                    eprintln!("the board is not in Wi-Fi mode (fenix radio wifi)");
                    return 1;
                }
                if status != scan_status::OK {
                    eprintln!("scan failed");
                    return 1;
                }
                let auth = ["open", "WEP", "WPA", "WPA2", "WPA/WPA2", "enterprise", "WPA3", "WPA2/WPA3", "other"];
                for a in &aps {
                    let b = a.bssid.map(|x| format!("{x:02x}")).join(":");
                    println!(
                        "{:>4} dBm  ch {:>2}  {:<10} {}  ({b})",
                        a.rssi,
                        a.channel,
                        auth.get(a.auth as usize).copied().unwrap_or("?"),
                        a.ssid
                    );
                }
                println!("{} network(s)", aps.len());
                return 0;
            }
            Some(Event::Msg(Msg::RadioState(s))) => eprintln!("{}", radio_text(&s)),
            Some(Event::Msg(Msg::Error { code, text })) => {
                eprintln!("board error {code}: {text} (firmware before 0.1.8?)");
                return 1;
            }
            _ => {}
        }
    }
    eprintln!("no WIFI_SCAN answer");
    1
}

/// Two SYSINFO answers 1 s apart; load and CPU % come from the difference (as the app does).
fn sysinfo(opts: LinkOptions) -> i32 {
    let link = Link::spawn(opts);
    if !wait_connected(&link, 6) {
        eprintln!("no board");
        return 1;
    }
    let Some(a) = ask_sysinfo(&link) else {
        return 1;
    };
    std::thread::sleep(Duration::from_secs(1));
    let Some(b) = ask_sysinfo(&link) else {
        return 1;
    };
    let dt = b.time_us.wrapping_sub(a.time_us).max(1) as f64;
    let was = |id: u16| a.tasks.iter().find(|t| t.id == id).map(|t| t.runtime);
    for core in 0..2u8 {
        if let Some(idle) = b
            .tasks
            .iter()
            .find(|t| t.name.starts_with("IDLE") && t.core == core)
        {
            if let Some(r) = was(idle.id) {
                let load = 100.0 * (1.0 - idle.runtime.wrapping_sub(r) as f64 / dt);
                println!("core {core}: {:.1}% busy", load.clamp(0.0, 100.0));
            }
        }
    }
    println!(
        "{} MHz; heap {} free of {} (min {}, largest block {}, DMA {}); firmware {} of {}; flash {}; apps {} of {}; nvs {} of {} entries",
        b.cpu_mhz, b.heap_free, b.heap_total, b.heap_min, b.heap_largest, b.dma_free,
        b.app_size, b.app_slot, b.flash_size, b.apps_used, b.apps_total, b.nvs_used, b.nvs_total
    );
    println!(
        "link: rx {} B tx {} B, {} frames, {} errors; fps {:.1}; backlight {}; flags {:#04x}",
        b.rx_bytes,
        b.tx_bytes,
        b.rx_frames,
        b.rx_errors,
        b.fps_x10 as f64 / 10.0,
        b.backlight,
        b.flags
    );
    println!(
        "{:<16} {:>4} {:>4} {:>5} {:>7} {:>6}",
        "task", "core", "prio", "state", "cpu%", "stack"
    );
    for t in &b.tasks {
        let cpu = was(t.id).map_or("-".into(), |r| {
            format!(
                "{:.1}",
                100.0 * t.runtime.wrapping_sub(r) as f64 / (dt * 2.0)
            )
        });
        let core = if t.core == 255 {
            "any".into()
        } else {
            t.core.to_string()
        };
        println!(
            "{:<16} {:>4} {:>4} {:>5} {:>7} {:>6}",
            t.name, core, t.priority, t.state, cpu, t.stack_free
        );
    }
    0
}

/// Press and release at (x, y) through the TOUCH message.
fn tap(opts: LinkOptions, x: u16, y: u16) -> i32 {
    let link = Link::spawn(opts);
    if !wait_connected(&link, 6) {
        eprintln!("no board");
        return 1;
    }
    link.send(Msg::Touch { x, y, down: true });
    std::thread::sleep(Duration::from_millis(120));
    link.send(Msg::Touch { x, y, down: false });
    // Print what the tap caused (EVENT, BOARD_STATE, APP_OPENED, ...).
    let end = Instant::now() + Duration::from_millis(400);
    while Instant::now() < end {
        match link.recv_event(Duration::from_millis(50)) {
            Some(Event::Msg(Msg::Stats(_))) | Some(Event::Connected { .. }) | None => {}
            Some(e) => print_event(&e),
        }
    }
    0
}

/// Press at `a`, drag to `b` over ~250 ms, release.
fn swipe(opts: LinkOptions, a: (u16, u16), b: (u16, u16)) -> i32 {
    let link = Link::spawn(opts);
    if !wait_connected(&link, 6) {
        eprintln!("no board");
        return 1;
    }
    const STEPS: i32 = 12;
    for i in 0..=STEPS {
        let x = a.0 as i32 + (b.0 as i32 - a.0 as i32) * i / STEPS;
        let y = a.1 as i32 + (b.1 as i32 - a.1 as i32) * i / STEPS;
        link.send(Msg::Touch {
            x: x as u16,
            y: y as u16,
            down: true,
        });
        std::thread::sleep(Duration::from_millis(20));
    }
    link.send(Msg::Touch {
        x: b.0,
        y: b.1,
        down: false,
    });
    let end = Instant::now() + Duration::from_millis(400);
    while Instant::now() < end {
        match link.recv_event(Duration::from_millis(50)) {
            Some(Event::Msg(Msg::Stats(_))) | Some(Event::Connected { .. }) | None => {}
            Some(e) => print_event(&e),
        }
    }
    0
}

fn wait_connected(link: &Link, secs: u64) -> bool {
    let end = Instant::now() + Duration::from_secs(secs);
    while Instant::now() < end {
        if let Some(e) = link.recv_event(Duration::from_millis(200)) {
            if matches!(e, Event::Connected { .. }) {
                print_event(&e);
                return true;
            }
            print_event(&e);
        }
    }
    false
}

/// Stream benchmark: stream while opening and closing Settings every second
/// (landscape Tiles home), then report throughput and what the board dropped.
fn bench(opts: LinkOptions, secs: u64, nostream: bool) -> i32 {
    let link = Link::spawn(opts);
    if !wait_connected(&link, 6) {
        eprintln!("no board");
        return 1;
    }
    if !nostream {
        link.send(Msg::StreamStart { fps: 10 });
        link.send(Msg::StreamSync);
    }
    let mut canvas = StreamCanvas::default();
    let (mut rects, mut bytes, mut px) = (0u32, 0usize, 0u64);
    let mut stats: Vec<Stats> = Vec::new();
    let t0 = Instant::now();
    let end = t0 + Duration::from_secs(secs.max(2));
    let mut next_tap = t0 + Duration::from_millis(500);
    let mut open = false;
    while Instant::now() < end {
        if Instant::now() >= next_tap {
            let (x, y) = if open { (30, 16) } else { (58, 200) };
            link.send(Msg::Touch { x, y, down: true });
            std::thread::sleep(Duration::from_millis(60));
            link.send(Msg::Touch { x, y, down: false });
            open = !open;
            next_tap += Duration::from_secs(1);
        }
        if let Some(Event::Msg(m)) = link.recv_event(Duration::from_millis(20)) {
            match &m {
                Msg::StreamRect(r) => bytes += r.data.len(),
                Msg::Stats(s) => stats.push(s.clone()),
                Msg::Log { text, .. } => println!("log: {text}"),
                _ => {}
            }
            if let Some(d) = canvas.apply(&m) {
                rects += 1;
                px += d.w as u64 * d.h as u64;
            }
        }
    }
    link.send(Msg::StreamStop);
    let dt = t0.elapsed().as_secs_f32();
    let full = (canvas.width as u64 * canvas.height as u64).max(1);
    println!(
        "{secs} s: {rects} rects, {:.1} KB/s, {:.1} screens/s of pixels, {:.1}x RLE ratio",
        bytes as f32 / 1024.0 / dt,
        px as f32 / full as f32 / dt,
        (px * 2) as f32 / bytes.max(1) as f32
    );
    if let (Some(a), Some(b)) = (stats.first(), stats.last()) {
        println!(
            "board: fps {:.1}, heap {} (min {}), stream dropped +{}, rx errors {}",
            b.fps_x10 as f32 / 10.0,
            b.free_heap,
            b.min_heap,
            b.stream_dropped - a.stream_dropped,
            b.rx_errors
        );
    }
    0
}

fn shot(opts: LinkOptions, out: &str, secs: u64) -> i32 {
    let link = Link::spawn(opts);
    if !wait_connected(&link, 6) {
        eprintln!("no board");
        return 1;
    }
    link.send(Msg::StreamStart { fps: 10 });
    link.send(Msg::StreamSync);
    let mut canvas = StreamCanvas::default();
    let (mut rects, mut bytes) = (0u32, 0usize);
    let t0 = Instant::now();
    let end = t0 + Duration::from_secs(secs.max(1));
    while Instant::now() < end {
        if let Some(Event::Msg(m)) = link.recv_event(Duration::from_millis(100)) {
            if let Msg::StreamRect(r) = &m {
                bytes += r.data.len();
            }
            if canvas.apply(&m).is_some() {
                rects += 1;
            }
        }
    }
    link.send(Msg::StreamStop);
    std::thread::sleep(Duration::from_millis(200));
    let dt = t0.elapsed().as_secs_f32();
    println!(
        "{rects} rects, {:.1} KB/s, canvas {}x{}",
        bytes as f32 / 1024.0 / dt,
        canvas.width,
        canvas.height
    );
    match write_png(
        out,
        canvas.width as u32,
        canvas.height as u32,
        &canvas.to_rgba(),
    ) {
        Ok(()) => {
            println!("saved {out}");
            0
        }
        Err(e) => {
            eprintln!("png: {e}");
            1
        }
    }
}

fn write_png(path: &str, w: u32, h: u32, rgba: &[u8]) -> Result<(), Box<dyn std::error::Error>> {
    let f = std::fs::File::create(path)?;
    let mut enc = png::Encoder::new(std::io::BufWriter::new(f), w, h);
    enc.set_color(png::ColorType::Rgba);
    enc.set_depth(png::BitDepth::Eight);
    enc.write_header()?.write_image_data(rgba)?;
    Ok(())
}

fn list(opts: LinkOptions) -> i32 {
    let link = Link::spawn(opts);
    if !wait_connected(&link, 6) {
        return 1;
    }
    link.send(Msg::AppListReq);
    let end = Instant::now() + Duration::from_secs(3);
    while Instant::now() < end {
        if let Some(Event::Msg(Msg::AppList(v))) = link.recv_event(Duration::from_millis(100)) {
            for e in &v {
                println!(
                    "{} {} {}",
                    e.id,
                    e.version,
                    e.sha256
                        .iter()
                        .take(6)
                        .map(|b| format!("{b:02x}"))
                        .collect::<String>()
                );
            }
            println!("{} app(s)", v.len());
            return 0;
        }
    }
    1
}

/// Install a package: id and version come from the package itself. Keeps 4 chunks in flight and
/// goes back to the last acked offset when acks stop for a second (a chunk was lost).
fn install(opts: LinkOptions, file: &Path) -> i32 {
    let data = match std::fs::read(file) {
        Ok(d) => d,
        Err(e) => {
            eprintln!("{e}");
            return 1;
        }
    };
    let pkg = match fenix_pkg::parse(&data) {
        Ok(p) => p,
        Err(e) => {
            eprintln!("{}: {e}", file.display());
            return 1;
        }
    };
    let sha: [u8; 32] = Sha256::digest(&data).into();
    let link = Link::spawn(opts);
    if !wait_connected(&link, 6) {
        return 1;
    }
    let t0 = Instant::now();
    link.send(Msg::AppInstallBegin {
        id: pkg.id().into(),
        version: pkg.version().into(),
        size: data.len() as u32,
        sha256: sha,
    });
    let (mut sent, mut acked, mut ended) = (0usize, 0usize, false);
    let mut last_ack = Instant::now();
    const WINDOW: usize = 4 * fenix_proto::MAX_CHUNK;
    let end = Instant::now() + Duration::from_secs(60);
    while Instant::now() < end {
        if last_ack.elapsed() > Duration::from_secs(1) && sent > acked {
            sent = acked;
            last_ack = Instant::now();
        }
        while sent < data.len() && sent < acked + WINDOW {
            let n = (data.len() - sent).min(fenix_proto::MAX_CHUNK);
            link.send(Msg::AppChunk {
                offset: sent as u32,
                data: data[sent..sent + n].to_vec(),
            });
            sent += n;
        }
        if acked == data.len() && !ended {
            link.send(Msg::AppInstallEnd);
            ended = true;
        }
        match link.recv_event(Duration::from_millis(50)) {
            Some(Event::Msg(Msg::AppAck { next_offset })) => {
                acked = next_offset as usize;
                last_ack = Instant::now();
            }
            Some(Event::Msg(Msg::AppResult { op, code, id, text })) => {
                println!(
                    "{} v{} ({} B): op {op} code {code} {id} {text} in {:.2} s",
                    pkg.id(),
                    pkg.version(),
                    data.len(),
                    t0.elapsed().as_secs_f32()
                );
                return if code == app::OK { 0 } else { 1 };
            }
            Some(e) => print_event(&e),
            None => {}
        }
    }
    eprintln!("timeout");
    1
}

/// Set bindings of an app like its PC half would (`cpu=42`, `on=true`, `cpu_text=42 %`), then
/// print what the board sends back (EVENT, APP_OPENED, ...) for 3 s.
fn set(opts: LinkOptions, file: &Path, pairs: &[String]) -> i32 {
    let pkg = match std::fs::read(file)
        .map_err(|e| e.to_string())
        .and_then(|b| fenix_pkg::parse(&b))
    {
        Ok(p) => p,
        Err(e) => {
            eprintln!("{}: {e}", file.display());
            return 1;
        }
    };
    let mut msgs = Vec::new();
    for pair in pairs {
        let Some((name, v)) = pair.split_once('=') else {
            eprintln!("expected <bind>=<value>, got {pair}");
            return 2;
        };
        let Some(bind) = pkg.bind_id(name) else {
            eprintln!("no binding {name:?}; bindings: {:?}", pkg.bind_names());
            return 2;
        };
        let value = match (v.parse::<i32>(), v) {
            (Ok(n), _) => BindValue::Int(n),
            (_, "true") => BindValue::Bool(true),
            (_, "false") => BindValue::Bool(false),
            _ => BindValue::Text(v.into()),
        };
        msgs.push(Msg::BindSet {
            bind,
            id: pkg.id().into(),
            value,
        });
    }
    let link = Link::spawn(opts);
    if !wait_connected(&link, 6) {
        return 1;
    }
    for m in msgs {
        link.send(m);
    }
    let end = Instant::now() + Duration::from_secs(3);
    while Instant::now() < end {
        if let Some(e) = link.recv_event(Duration::from_millis(100)) {
            match e {
                Event::Msg(Msg::Event {
                    elem, kind, value, ..
                }) => println!(
                    "{} event {:?} kind {kind} value {value}",
                    ts(),
                    pkg.event_name(elem).unwrap_or("?")
                ),
                Event::Msg(Msg::Stats(_)) | Event::Connected { .. } => {}
                e => print_event(&e),
            }
        }
    }
    0
}

fn remove(opts: LinkOptions, id: &str) -> i32 {
    let link = Link::spawn(opts);
    if !wait_connected(&link, 6) {
        return 1;
    }
    link.send(Msg::AppRemove { id: id.into() });
    let end = Instant::now() + Duration::from_secs(5);
    while Instant::now() < end {
        if let Some(Event::Msg(Msg::AppResult { code, text, .. })) =
            link.recv_event(Duration::from_millis(100))
        {
            println!("remove: code {code} {text}");
            return if code == app::OK { 0 } else { 1 };
        }
    }
    1
}

fn soak(opts: LinkOptions, minutes: u64) -> i32 {
    let link = Link::spawn(opts);
    let end = Instant::now() + Duration::from_secs(minutes * 60);
    let (mut connects, mut disconnects, mut stats) = (0u32, 0u32, None);
    let mut rtt_max = Duration::ZERO;
    while Instant::now() < end {
        match link.recv_event(Duration::from_millis(500)) {
            Some(Event::Connected { .. }) => connects += 1,
            Some(Event::Disconnected { reason }) => {
                disconnects += 1;
                println!("{} disconnected: {reason}", ts());
            }
            Some(Event::Rtt(d)) => rtt_max = rtt_max.max(d),
            Some(Event::Msg(Msg::Stats(s))) => stats = Some(s),
            _ => {}
        }
    }
    println!("soak {minutes} min: connects {connects}, disconnects {disconnects}, max rtt {} ms, last stats {stats:?}", rtt_max.as_millis());
    if connects == 1 && disconnects == 0 {
        0
    } else {
        1
    }
}
