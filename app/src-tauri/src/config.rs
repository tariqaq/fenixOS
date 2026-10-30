//! User settings and installed-app state in `%APPDATA%\fenixOS\config.json` (PLAN §3.5).
//! Written whole to a temp file and renamed, so a crash never leaves half a file.

use std::collections::BTreeMap;
use std::fs;
use std::path::PathBuf;

use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct Config {
    #[serde(default)]
    pub apps: BTreeMap<String, AppCfg>,
    /// Connection port: `None` finds the board on any CH340 port.
    #[serde(default)]
    pub port: Option<String>,
    #[serde(default = "yes")]
    pub check_updates: bool,
    /// Set once the first-run experience finished (P9); "Show welcome again" clears it.
    #[serde(default)]
    pub welcome_done: bool,
    /// Settings > Weather (P34): the board's weather place, sent on every connect.
    #[serde(default)]
    pub weather: Option<crate::weather::Place>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AppCfg {
    /// Installed version: files are in `apps\<id>\<version>\`.
    pub version: String,
    pub enabled: bool,
    #[serde(default)]
    pub settings: Map<String, Value>,
}

fn yes() -> bool {
    true
}

pub fn path() -> PathBuf {
    std::env::var_os("APPDATA")
        .map(PathBuf::from)
        .unwrap_or_else(std::env::temp_dir)
        .join("fenixOS")
        .join("config.json")
}

impl Config {
    pub fn load() -> Config {
        fs::read(path())
            .ok()
            .and_then(|b| serde_json::from_slice(&b).ok())
            .unwrap_or_else(|| Config {
                check_updates: true,
                ..Default::default()
            })
    }

    pub fn save(&self) -> std::io::Result<()> {
        let p = path();
        if let Some(dir) = p.parent() {
            fs::create_dir_all(dir)?;
        }
        let tmp = p.with_extension("json.tmp");
        fs::write(&tmp, serde_json::to_vec_pretty(self)?)?;
        fs::rename(tmp, p)
    }
}
