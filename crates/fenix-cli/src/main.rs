//! `fenix`: developer CLI for the fenixOS board link.
//!
//! fenix ports                      list serial ports (CH340 marked)
//! fenix hello                      handshake and print the board info
//! fenix logs [secs]                print LOG / STATS / BOARD_STATE (default 30 s)
//! fenix time                       send TIME_SET (now, UTC)
//! fenix reboot                     ask the board to restart
//! fenix shot <out.png> [secs]      stream the screen for a while, save the last frame
//! fenix list                       installed apps
//! fenix install <file.fxp> <id> <version>   push a board package
//! fenix remove <id>                remove an app
//! fenix open <id> | close          (debug) ask nothing; apps open on the board
//! fenix soak <minutes>             ping + stats soak, counts errors and reconnects
//! Common option: --port COMx (default: auto-detect).

use std::path::Path;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use fenix_link::{Event, Link, LinkOptions, StreamCanvas};
use fenix_proto::{app, Msg};
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
                println!("{} {:04x}:{:04x} {}{}", p.name, p.vid, p.pid, p.product.unwrap_or_default(), if board { "  <- board" } else { "" });
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
        "shot" => shot(opts, args.get(1).map(String::as_str).unwrap_or("shot.png"), arg_u64(&args, 2, 3)),
        "list" => list(opts),
        "install" => match (args.get(1), args.get(2), args.get(3)) {
            (Some(f), Some(id), Some(v)) => install(opts, Path::new(f), id, v),
            _ => usage(),
        },
        "remove" => match args.get(1) {
            Some(id) => remove(opts, id),
            None => usage(),
        },
        "soak" => soak(opts, arg_u64(&args, 1, 60)),
        _ => usage(),
    };
    std::process::exit(code);
}

fn usage() -> i32 {
    eprintln!("usage: fenix ports|hello|logs [s]|time|reboot|shot <png> [s]|list|install <fxp> <id> <ver>|remove <id>|soak <min> [--port COMx]");
    2
}

fn arg_u64(args: &[String], i: usize, def: u64) -> u64 {
    args.get(i).and_then(|s| s.parse().ok()).unwrap_or(def)
}

fn now_s() -> u32 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs() as u32).unwrap_or(0)
}

fn ts() -> String {
    let ms = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_millis()).unwrap_or(0);
    let s = (ms / 1000) % 86_400;
    format!("{:02}:{:02}:{:02}.{:03}", (s / 3600 + 8) % 24, (s / 60) % 60, s % 60, ms % 1000)
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
    println!("{rects} rects, {:.1} KB/s, canvas {}x{}", bytes as f32 / 1024.0 / dt, canvas.width, canvas.height);
    match write_png(out, canvas.width as u32, canvas.height as u32, &canvas.to_rgba()) {
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
                println!("{} {} {}", e.id, e.version, e.sha256.iter().take(6).map(|b| format!("{b:02x}")).collect::<String>());
            }
            println!("{} app(s)", v.len());
            return 0;
        }
    }
    1
}

fn install(opts: LinkOptions, file: &Path, id: &str, version: &str) -> i32 {
    let data = match std::fs::read(file) {
        Ok(d) => d,
        Err(e) => {
            eprintln!("{e}");
            return 1;
        }
    };
    let sha: [u8; 32] = Sha256::digest(&data).into();
    let link = Link::spawn(opts);
    if !wait_connected(&link, 6) {
        return 1;
    }
    let t0 = Instant::now();
    link.send(Msg::AppInstallBegin { id: id.into(), version: version.into(), size: data.len() as u32, sha256: sha });
    let mut sent = 0usize;
    let mut acked = 0usize;
    const WINDOW: usize = 4 * fenix_proto::MAX_CHUNK;
    let end = Instant::now() + Duration::from_secs(60);
    while Instant::now() < end {
        while sent < data.len() && sent < acked + WINDOW {
            let n = (data.len() - sent).min(fenix_proto::MAX_CHUNK);
            link.send(Msg::AppChunk { offset: sent as u32, data: data[sent..sent + n].to_vec() });
            sent += n;
        }
        if acked == data.len() && sent == data.len() {
            link.send(Msg::AppInstallEnd);
            sent += 1; // only once
        }
        match link.recv_event(Duration::from_millis(100)) {
            Some(Event::Msg(Msg::AppAck { next_offset })) => acked = next_offset as usize,
            Some(Event::Msg(Msg::AppResult { op, code, id, text })) => {
                println!("result op {op} code {code} {id} {text} in {:.2} s", t0.elapsed().as_secs_f32());
                return if code == app::OK { 0 } else { 1 };
            }
            Some(e) => print_event(&e),
            None => {}
        }
    }
    eprintln!("timeout");
    1
}

fn remove(opts: LinkOptions, id: &str) -> i32 {
    let link = Link::spawn(opts);
    if !wait_connected(&link, 6) {
        return 1;
    }
    link.send(Msg::AppRemove { id: id.into() });
    let end = Instant::now() + Duration::from_secs(5);
    while Instant::now() < end {
        if let Some(Event::Msg(Msg::AppResult { code, text, .. })) = link.recv_event(Duration::from_millis(100)) {
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
