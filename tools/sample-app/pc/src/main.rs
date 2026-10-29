//! PC half of the test fixture app: sends CPU and memory use to the board while the app is open,
//! and logs what the user does on the board (docs/sidecars.md).
#![windows_subsystem = "windows"]

use std::time::{Duration, Instant};

use fenix_sdk::{App, Json, Level, Message};
use windows_sys::Win32::Foundation::FILETIME;
use windows_sys::Win32::System::SystemInformation::{GlobalMemoryStatusEx, MEMORYSTATUSEX};
use windows_sys::Win32::System::Threading::GetSystemTimes;

fn main() {
    let Ok(app) = App::connect() else {
        std::process::exit(2);
    };
    app.log_info(format!("Sample PC half v{} started", app.version));
    let mut cpu = Cpu::new();
    let mut next = Instant::now();
    loop {
        let wait = next.saturating_duration_since(Instant::now());
        match app.next(wait) {
            Some(Message::Stop) => break,
            Some(Message::Opened) => {
                // Restore the slider from the settings, and send stats right away.
                let level = app
                    .settings()
                    .get("level")
                    .and_then(Json::as_i64)
                    .unwrap_or(50);
                app.set("level", level as i32);
                next = Instant::now();
            }
            Some(Message::Event { name, kind, value }) => match name.as_str() {
                "hello" => {
                    let greeting = app
                        .settings()
                        .get("greeting")
                        .and_then(|v| v.as_str().map(String::from));
                    app.log_info(greeting.unwrap_or_else(|| "Hello".into()));
                }
                _ => app.log_info(format!("Board event: {name} ({kind:?}, {value})")),
            },
            Some(Message::Button { key }) if key == "ping" => {
                app.log(Level::Info, "Test log line from the sample app")
            }
            Some(_) => {}
            None => {}
        }
        if Instant::now() >= next {
            let every = app
                .settings()
                .get("interval")
                .and_then(|v| v.as_str()?.parse::<u64>().ok())
                .unwrap_or(1);
            next = Instant::now() + Duration::from_secs(every.clamp(1, 60));
            let c = cpu.percent();
            if app.is_open() {
                app.set("cpu", c);
                app.set("cpu_text", format!("{c} %"));
                if app
                    .settings()
                    .get("memory")
                    .and_then(Json::as_bool)
                    .unwrap_or(true)
                {
                    let m = memory_percent();
                    app.set("mem", m);
                    app.set("mem_text", format!("{m} %"));
                } else {
                    app.set("mem", 0);
                    app.set("mem_text", "--");
                }
            }
        }
    }
}

struct Cpu {
    idle: u64,
    total: u64,
}

impl Cpu {
    fn new() -> Self {
        let (idle, total) = system_times();
        Cpu { idle, total }
    }

    /// Busy share of all cores since the last call, 0..=100.
    fn percent(&mut self) -> i32 {
        let (idle, total) = system_times();
        let (di, dt) = (idle - self.idle, total - self.total);
        (self.idle, self.total) = (idle, total);
        if dt == 0 {
            0
        } else {
            (100 - di * 100 / dt) as i32
        }
    }
}

// (idle, kernel + user) in 100 ns units; kernel time includes idle time.
fn system_times() -> (u64, u64) {
    let ft = || FILETIME {
        dwLowDateTime: 0,
        dwHighDateTime: 0,
    };
    let (mut i, mut k, mut u) = (ft(), ft(), ft());
    unsafe { GetSystemTimes(&mut i, &mut k, &mut u) };
    let v = |f: FILETIME| (f.dwHighDateTime as u64) << 32 | f.dwLowDateTime as u64;
    (v(i), v(k) + v(u))
}

fn memory_percent() -> i32 {
    let mut m: MEMORYSTATUSEX = unsafe { std::mem::zeroed() };
    m.dwLength = std::mem::size_of::<MEMORYSTATUSEX>() as u32;
    unsafe { GlobalMemoryStatusEx(&mut m) };
    m.dwMemoryLoad as i32
}
