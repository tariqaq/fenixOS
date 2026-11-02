//! `secret` app settings (P51-M): plain only in memory and on the pipe to the app's PC half; in
//! `config.json` each value is DPAPI-encrypted for the current Windows user (CryptProtectData,
//! user scope, a fixed entropy) and stored as `dpapi:<base64>`. Never logged: `Debug` prints the
//! keys only. A value that cannot be decrypted (another user, another PC) is dropped, so the
//! setting shows as not set; one that cannot be encrypted is not written.

use std::fmt;
use std::ptr::{null, null_mut};

use base64::Engine as _;
use serde::{Deserialize, Deserializer, Serialize, Serializer};
use serde_json::{Map, Value};
use windows_sys::Win32::Foundation::LocalFree;
use windows_sys::Win32::Security::Cryptography::{
    CryptProtectData, CryptUnprotectData, CRYPTPROTECT_UI_FORBIDDEN, CRYPT_INTEGER_BLOB,
};

const PREFIX: &str = "dpapi:";
const ENTROPY: &[u8] = b"fenixOS app setting";

fn blob(b: &[u8]) -> CRYPT_INTEGER_BLOB {
    CRYPT_INTEGER_BLOB {
        cbData: b.len() as u32,
        pbData: b.as_ptr() as *mut u8,
    }
}

fn dpapi(data: &[u8], protect: bool) -> Option<Vec<u8>> {
    let (input, entropy) = (blob(data), blob(ENTROPY));
    let mut out = CRYPT_INTEGER_BLOB {
        cbData: 0,
        pbData: null_mut(),
    };
    // SAFETY: the in blobs point at live slices; on success `out` holds a LocalAlloc buffer that
    // is copied and freed here.
    unsafe {
        let ok = if protect {
            CryptProtectData(
                &input,
                null(),
                &entropy,
                null(),
                null(),
                CRYPTPROTECT_UI_FORBIDDEN,
                &mut out,
            )
        } else {
            CryptUnprotectData(
                &input,
                null_mut(),
                &entropy,
                null(),
                null(),
                CRYPTPROTECT_UI_FORBIDDEN,
                &mut out,
            )
        };
        if ok == 0 || out.pbData.is_null() {
            return None;
        }
        let v = std::slice::from_raw_parts(out.pbData, out.cbData as usize).to_vec();
        LocalFree(out.pbData as _);
        Some(v)
    }
}

pub fn protect(plain: &str) -> Option<String> {
    let enc = dpapi(plain.as_bytes(), true)?;
    Some(format!(
        "{PREFIX}{}",
        base64::engine::general_purpose::STANDARD.encode(enc)
    ))
}

pub fn unprotect(stored: &str) -> Option<String> {
    let b = base64::engine::general_purpose::STANDARD
        .decode(stored.strip_prefix(PREFIX)?)
        .ok()?;
    String::from_utf8(dpapi(&b, false)?).ok()
}

/// An app's secret values by key, plain in memory.
#[derive(Clone, Default, PartialEq)]
pub struct Secrets(pub Map<String, Value>);

impl fmt::Debug for Secrets {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_set().entries(self.0.keys()).finish()
    }
}

impl Secrets {
    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }
}

impl Serialize for Secrets {
    fn serialize<S: Serializer>(&self, s: S) -> Result<S::Ok, S::Error> {
        let enc: Map<String, Value> = self
            .0
            .iter()
            .filter_map(|(k, v)| Some((k.clone(), Value::String(protect(v.as_str()?)?))))
            .collect();
        enc.serialize(s)
    }
}

impl<'de> Deserialize<'de> for Secrets {
    fn deserialize<D: Deserializer<'de>>(d: D) -> Result<Self, D::Error> {
        let enc = Map::<String, Value>::deserialize(d)?;
        Ok(Secrets(
            enc.into_iter()
                .filter_map(|(k, v)| Some((k, Value::String(unprotect(v.as_str()?)?))))
                .collect(),
        ))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip_never_plain() {
        let mut s = Secrets::default();
        s.0.insert("token".into(), "hunter2-ключ".into());
        let json = serde_json::to_string(&s).unwrap();
        assert!(!json.contains("hunter2") && json.contains(PREFIX), "{json}");
        let back: Secrets = serde_json::from_str(&json).unwrap();
        assert_eq!(back, s);
        assert_eq!(format!("{s:?}"), "{\"token\"}");
        // Not decryptable (tampered, another user): dropped, not an error.
        let bad: Secrets =
            serde_json::from_str(r#"{"token": "dpapi:AAAA", "plain": "x"}"#).unwrap();
        assert!(bad.is_empty());
    }
}
