//! Logs: a rolling file (3 × 1 MB in `%LOCALAPPDATA%\fenixOS\logs`) plus the last lines in memory
//! for the log sheet (PcLogs artboard). Lines read like the artboard: time, source, plain words.

use std::collections::VecDeque;
use std::fs::{self, File, OpenOptions};
use std::io::Write;
use std::path::PathBuf;
use std::sync::Mutex;

use serde::Serialize;

const MAX_FILE: u64 = 1024 * 1024;
const FILES: usize = 3;
const KEEP: usize = 2000;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum Source {
    Pc,
    Board,
    Apps,
}

#[derive(Debug, Clone, Serialize)]
pub struct Line {
    /// Local time, HH:MM:SS.
    pub time: String,
    pub source: Source,
    /// A warning or error: shown under "Problems" and in `warning` colour.
    pub problem: bool,
    pub text: String,
}

struct Inner {
    file: Option<File>,
    size: u64,
    recent: VecDeque<Line>,
}

pub struct Logs {
    dir: PathBuf,
    inner: Mutex<Inner>,
}

impl Logs {
    pub fn new(dir: PathBuf) -> Self {
        let _ = fs::create_dir_all(&dir);
        let path = dir.join("fenixos.log");
        let size = fs::metadata(&path).map(|m| m.len()).unwrap_or(0);
        let file = OpenOptions::new()
            .create(true)
            .append(true)
            .open(&path)
            .ok();
        Self {
            dir,
            inner: Mutex::new(Inner {
                file,
                size,
                recent: VecDeque::new(),
            }),
        }
    }

    pub fn dir(&self) -> &PathBuf {
        &self.dir
    }

    pub fn add(&self, source: Source, problem: bool, text: impl Into<String>) -> Line {
        let line = Line {
            time: local_time(),
            source,
            problem,
            text: text.into(),
        };
        let mut g = self.inner.lock().unwrap();
        let tag = match source {
            Source::Pc => "PC",
            Source::Board => "Board",
            Source::Apps => "Apps",
        };
        let row = format!(
            "{} {}{} {}\n",
            line.time,
            tag,
            if problem { " !" } else { "" },
            line.text
        );
        if g.size + row.len() as u64 > MAX_FILE {
            self.rotate(&mut g);
        }
        if let Some(f) = g.file.as_mut() {
            if f.write_all(row.as_bytes()).is_ok() {
                g.size += row.len() as u64;
            }
        }
        if g.recent.len() == KEEP {
            g.recent.pop_front();
        }
        g.recent.push_back(line.clone());
        line
    }

    pub fn recent(&self) -> Vec<Line> {
        self.inner.lock().unwrap().recent.iter().cloned().collect()
    }

    // fenixos.log -> fenixos.1.log -> fenixos.2.log (oldest dropped).
    fn rotate(&self, g: &mut Inner) {
        g.file = None;
        let name = |i: usize| {
            self.dir.join(if i == 0 {
                "fenixos.log".to_string()
            } else {
                format!("fenixos.{i}.log")
            })
        };
        let _ = fs::remove_file(name(FILES - 1));
        for i in (0..FILES - 1).rev() {
            let _ = fs::rename(name(i), name(i + 1));
        }
        g.file = OpenOptions::new()
            .create(true)
            .append(true)
            .open(name(0))
            .ok();
        g.size = 0;
    }
}

/// Local wall-clock time as HH:MM:SS (Windows: GetLocalTime, no extra crate).
pub fn local_time() -> String {
    #[repr(C)]
    #[derive(Default)]
    struct SystemTime {
        year: u16,
        month: u16,
        dow: u16,
        day: u16,
        hour: u16,
        minute: u16,
        second: u16,
        ms: u16,
    }
    #[link(name = "kernel32")]
    extern "system" {
        fn GetLocalTime(t: *mut SystemTime);
    }
    let mut t = SystemTime::default();
    unsafe { GetLocalTime(&mut t) };
    format!("{:02}:{:02}:{:02}", t.hour, t.minute, t.second)
}
