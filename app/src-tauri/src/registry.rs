//! The app registry (docs/registry.md): read the index, check manifests, download app files.
//! `FENIX_REGISTRY=<folder>` reads a local registry instead of GitHub.

use std::path::PathBuf;
use std::time::Duration;

use serde::{Deserialize, Serialize};
use serde_json::Value;
use sha2::{Digest, Sha256};

const INDEX_URL: &str =
    "https://raw.githubusercontent.com/tariqaq/fenixOS/main/registry/index.json";
const ALLOWED_URL: &str = "https://github.com/tariqaq/";
const MAX_BOARD: u64 = 64 * 1024;
const MAX_PC: u64 = 64 * 1024 * 1024;
const PERMISSIONS: [&str; 6] = [
    "keys",
    "media",
    "system-stats",
    "network",
    "files",
    "programs",
];

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Download {
    pub url: String,
    pub sha256: String,
    pub size: u64,
    #[serde(default)]
    pub elevated: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Manifest {
    pub id: String,
    pub name: String,
    pub description: String,
    pub version: String,
    pub min_fenixos: String,
    pub color: String,
    pub icon: String,
    pub repo: String,
    pub board: Download,
    #[serde(default)]
    pub pc: Option<Download>,
    #[serde(default)]
    pub permissions: Vec<String>,
    /// Settings schema, passed to the UI as is (checked in `check`).
    #[serde(default)]
    pub settings: Vec<Value>,
}

#[derive(Deserialize)]
struct Index {
    format: u32,
    apps: Vec<Value>,
}

/// Where the index came from; relative URLs resolve against a local folder.
#[derive(Debug, Clone)]
pub enum Source {
    GitHub,
    Local(PathBuf),
}

pub fn source() -> Source {
    match std::env::var_os("FENIX_REGISTRY") {
        Some(p) if !p.is_empty() => Source::Local(PathBuf::from(p)),
        _ => Source::GitHub,
    }
}

/// Every valid app in the registry. Invalid manifests are skipped and reported in the second list.
pub fn fetch(src: &Source) -> Result<(Vec<Manifest>, Vec<String>), String> {
    let bytes = match src {
        Source::GitHub => http_get(INDEX_URL, 4 * 1024 * 1024)?,
        Source::Local(dir) => std::fs::read(dir.join("index.json"))
            .map_err(|e| format!("{}: {e}", dir.join("index.json").display()))?,
    };
    let idx: Index = serde_json::from_slice(&bytes).map_err(|e| format!("registry index: {e}"))?;
    if idx.format != 1 {
        return Err(format!(
            "registry index format {} is newer than this fenixOS",
            idx.format
        ));
    }
    let local = matches!(src, Source::Local(_));
    let (mut ok, mut bad) = (Vec::new(), Vec::new());
    for v in idx.apps {
        let id = v
            .get("id")
            .and_then(Value::as_str)
            .unwrap_or("?")
            .to_string();
        match serde_json::from_value::<Manifest>(v)
            .map_err(|e| e.to_string())
            .and_then(|m| check(&m, local).map(|_| m))
        {
            Ok(m) => ok.push(m),
            Err(e) => bad.push(format!("{id}: {e}")),
        }
    }
    Ok((ok, bad))
}

fn semver(s: &str) -> Option<(u32, u32, u32)> {
    let mut p = s.split('.').map(|x| x.parse::<u32>().ok());
    let v = (p.next()??, p.next()??, p.next()??);
    p.next().is_none().then_some(v)
}

/// `a` is at least `b` (both `x.y.z`).
pub fn version_at_least(a: &str, b: &str) -> bool {
    matches!((semver(a), semver(b)), (Some(a), Some(b)) if a >= b)
}

/// Same rules as `tools/registry.py` (docs/registry.md).
pub fn check(m: &Manifest, local: bool) -> Result<(), String> {
    let id_ok = m.id.starts_with("io.github.")
        && m.id.len() <= 48
        && m.id.split('.').count() >= 4
        && m.id
            .bytes()
            .all(|c| c.is_ascii_alphanumeric() || b"._-".contains(&c));
    if !id_ok {
        return Err("bad id".into());
    }
    if m.name.is_empty() || m.name.len() > 24 || m.description.len() > 100 {
        return Err("name or description too long".into());
    }
    if semver(&m.version).is_none() || semver(&m.min_fenixos).is_none() {
        return Err("version: x.y.z".into());
    }
    if !m.repo.starts_with(ALLOWED_URL) {
        return Err(format!("repo must start with {ALLOWED_URL}"));
    }
    let url_ok =
        |u: &str| u.starts_with(ALLOWED_URL) || (local && !u.contains("://") && !u.contains(".."));
    let dl_ok = |d: &Download, max: u64| {
        url_ok(&d.url)
            && d.size > 0
            && d.size <= max
            && d.sha256.len() == 64
            && d.sha256
                .bytes()
                .all(|c| c.is_ascii_digit() || (b'a'..=b'f').contains(&c))
    };
    if !dl_ok(&m.board, MAX_BOARD) {
        return Err("board download: url, size or sha256".into());
    }
    if let Some(pc) = &m.pc {
        if !dl_ok(pc, MAX_PC) {
            return Err("pc download: url, size or sha256".into());
        }
    }
    if m.permissions
        .iter()
        .any(|p| !PERMISSIONS.contains(&p.as_str()))
    {
        return Err("unknown permission".into());
    }
    for s in &m.settings {
        let t = s.get("type").and_then(Value::as_str).unwrap_or("");
        let key_ok = s
            .get("key")
            .and_then(Value::as_str)
            .is_some_and(|k| !k.is_empty() && k.len() <= 32);
        if !["toggle", "slider", "choice", "text", "button"].contains(&t) || !key_ok {
            return Err("bad setting".into());
        }
    }
    Ok(())
}

/// Default values of a manifest's settings (buttons have none).
pub fn default_settings(m: &Manifest) -> serde_json::Map<String, Value> {
    m.settings
        .iter()
        .filter_map(|s| {
            Some((
                s.get("key")?.as_str()?.to_string(),
                s.get("default")?.clone(),
            ))
        })
        .collect()
}

/// Download one file and check its size and sha256.
pub fn download(src: &Source, d: &Download) -> Result<Vec<u8>, String> {
    let bytes = match (src, d.url.contains("://")) {
        (Source::Local(dir), false) => {
            std::fs::read(dir.join(&d.url)).map_err(|e| format!("{}: {e}", d.url))?
        }
        _ if d.url.starts_with(ALLOWED_URL) => http_get(&d.url, d.size + 1)?,
        _ => return Err(format!("{}: not an allowed source", d.url)),
    };
    if bytes.len() as u64 != d.size {
        return Err(format!(
            "{}: {} bytes, manifest says {}",
            d.url,
            bytes.len(),
            d.size
        ));
    }
    let sha = hex(&Sha256::digest(&bytes));
    if sha != d.sha256 {
        return Err(format!("{}: checksum does not match the manifest", d.url));
    }
    Ok(bytes)
}

pub fn hex(b: &[u8]) -> String {
    b.iter().map(|x| format!("{x:02x}")).collect()
}

fn http_get(url: &str, limit: u64) -> Result<Vec<u8>, String> {
    let agent = ureq::Agent::config_builder()
        .timeout_global(Some(Duration::from_secs(60)))
        .user_agent(concat!("fenixOS/", env!("CARGO_PKG_VERSION")))
        .build()
        .new_agent();
    let mut resp = agent.get(url).call().map_err(|e| format!("{url}: {e}"))?;
    resp.body_mut()
        .with_config()
        .limit(limit)
        .read_to_vec()
        .map_err(|e| format!("{url}: {e}"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn versions() {
        assert!(version_at_least("0.1.0", "0.1.0"));
        assert!(version_at_least("0.10.0", "0.9.9"));
        assert!(!version_at_least("0.1.0", "0.2.0"));
        assert!(!version_at_least("0.1", "0.1.0"));
    }
}
