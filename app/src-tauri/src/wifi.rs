//! Wi-Fi networks this PC hears, for Settings > Wireless when the board is not in Wi-Fi mode
//! (P34). Read from `netsh wlan show networks mode=bssid`; only 2.4 GHz entries are kept, as
//! the board's ESP32 has no 5 GHz radio. Same shape as the board's WIFI_SCAN.

use fenix_proto::{wifi_auth, WifiAp};
use std::os::windows::process::CommandExt;
use std::process::Command;

const CREATE_NO_WINDOW: u32 = 0x0800_0000;

pub fn scan() -> Result<Vec<WifiAp>, String> {
    let out = Command::new("netsh")
        .args(["wlan", "show", "networks", "mode=bssid"])
        .creation_flags(CREATE_NO_WINDOW)
        .output()
        .map_err(|e| format!("netsh: {e}"))?;
    let text = String::from_utf8_lossy(&out.stdout);
    if !out.status.success() {
        return Err(text.lines().find(|l| !l.trim().is_empty()).unwrap_or("netsh failed").trim().into());
    }
    Ok(parse(&text))
}

fn auth(s: &str) -> u8 {
    let s = s.to_ascii_lowercase();
    if s.contains("enterprise") {
        wifi_auth::ENTERPRISE
    } else if s.contains("wpa3") && s.contains("wpa2") || s.contains("transition") {
        wifi_auth::WPA2_WPA3
    } else if s.contains("wpa3") {
        wifi_auth::WPA3
    } else if s.contains("wpa2") {
        wifi_auth::WPA2
    } else if s.contains("wpa") {
        wifi_auth::WPA
    } else if s.contains("wep") {
        wifi_auth::WEP
    } else if s.contains("open") {
        wifi_auth::OPEN
    } else {
        wifi_auth::OTHER
    }
}

fn parse(text: &str) -> Vec<WifiAp> {
    let mut out: Vec<WifiAp> = Vec::new();
    let (mut ssid, mut au) = (String::new(), wifi_auth::OTHER);
    let mut cur: Option<WifiAp> = None;
    let push = |ap: Option<WifiAp>, out: &mut Vec<WifiAp>| {
        let Some(a) = ap else { return };
        if a.ssid.is_empty() || a.channel == 0 || a.channel > 14 {
            return;
        }
        match out.iter_mut().find(|o| o.ssid == a.ssid) {
            Some(o) if o.rssi >= a.rssi => {}
            Some(o) => *o = a,
            None => out.push(a),
        }
    };
    for line in text.lines() {
        let Some((k, v)) = line.split_once(" : ") else { continue };
        let (k, v) = (k.trim(), v.trim());
        if k.starts_with("SSID ") {
            push(cur.take(), &mut out);
            ssid = v.to_string();
        } else if k == "Authentication" {
            au = auth(v);
        } else if k.starts_with("BSSID") {
            push(cur.take(), &mut out);
            let mut bssid = [0u8; 6];
            for (i, p) in v.split(':').take(6).enumerate() {
                bssid[i] = u8::from_str_radix(p, 16).unwrap_or(0);
            }
            cur = Some(WifiAp { ssid: ssid.clone(), rssi: -100, channel: 0, auth: au, bssid });
        } else if k == "Signal" {
            if let (Some(a), Ok(pct)) = (cur.as_mut(), v.trim_end_matches('%').parse::<i32>()) {
                a.rssi = (pct / 2 - 100) as i8; // Windows' quality %, roughly dBm
            }
        } else if k == "Channel" {
            if let Some(a) = cur.as_mut() {
                a.channel = v.parse().unwrap_or(0);
            }
        }
    }
    push(cur.take(), &mut out);
    out.sort_by(|a, b| b.rssi.cmp(&a.rssi));
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_netsh() {
        let t = "SSID 1 : Home\r\n    Network type            : Infrastructure\r\n    Authentication          : WPA2-Personal\r\n    BSSID 1                 : 14:4c:ff:20:f3:0c\r\n         Signal             : 88%\r\n         Channel            : 11\r\n    BSSID 2                 : 14:4c:ff:20:f3:0d\r\n         Signal             : 90%\r\n         Channel            : 36\r\n\r\nSSID 2 : Cafe\r\n    Authentication          : Open\r\n    BSSID 1                 : 01:02:03:04:05:06\r\n         Signal             : 40%\r\n         Channel            : 6\r\n";
        let v = parse(t);
        assert_eq!(v.len(), 2);
        assert_eq!(v[0].ssid, "Home");
        assert_eq!(v[0].channel, 11); // the 5 GHz BSSID (channel 36) is dropped
        assert_eq!(v[0].rssi, -56);
        assert_eq!(v[0].auth, wifi_auth::WPA2);
        assert_eq!(v[1].auth, wifi_auth::OPEN);
    }
}
