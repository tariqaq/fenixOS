//! Settings > Weather (P34): find a place with Open-Meteo's geocoding API. The board fetches the
//! weather itself (Open-Meteo, plain HTTP) for the place saved here and sent as WEATHER_SET.

use std::time::Duration;

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Place {
    /// "Kuala Lumpur, Malaysia": what Settings shows.
    pub name: String,
    pub lat: f64,
    pub lon: f64,
}

impl Place {
    /// The board's copy: degrees × 10000 and a name cut to 47 bytes (proto/spec.md).
    pub fn to_msg(&self) -> fenix_proto::Msg {
        let mut name = self.name.clone();
        while name.len() > 47 {
            name.pop();
        }
        fenix_proto::Msg::WeatherSet {
            lat_e4: (self.lat * 1e4).round() as i32,
            lon_e4: (self.lon * 1e4).round() as i32,
            name,
        }
    }
}

#[derive(Deserialize)]
struct Answer {
    #[serde(default)]
    results: Vec<Hit>,
}

#[derive(Deserialize)]
struct Hit {
    name: String,
    latitude: f64,
    longitude: f64,
    #[serde(default)]
    admin1: Option<String>,
    #[serde(default)]
    country: Option<String>,
}

pub fn search(query: &str) -> Result<Vec<Place>, String> {
    let agent = ureq::Agent::config_builder()
        .timeout_global(Some(Duration::from_secs(10)))
        .user_agent(concat!("fenixOS/", env!("CARGO_PKG_VERSION")))
        .build()
        .new_agent();
    let body = agent
        .get("https://geocoding-api.open-meteo.com/v1/search")
        .query("name", query.trim())
        .query("count", "8")
        .query("language", "en")
        .call()
        .map_err(|e| format!("place search: {e}"))?
        .body_mut()
        .with_config()
        .limit(256 * 1024)
        .read_to_string()
        .map_err(|e| format!("place search: {e}"))?;
    parse(&body)
}

fn parse(body: &str) -> Result<Vec<Place>, String> {
    let a: Answer = serde_json::from_str(body).map_err(|e| format!("place search: {e}"))?;
    Ok(a.results
        .into_iter()
        .map(|h| {
            let mut name = h.name;
            for part in [h.admin1, h.country].into_iter().flatten() {
                if !part.is_empty() && part != name {
                    name = format!("{name}, {part}");
                }
            }
            Place {
                name,
                lat: h.latitude,
                lon: h.longitude,
            }
        })
        .collect())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_geocoding() {
        let v = parse(r#"{"results":[{"name":"Kuala Lumpur","latitude":3.1412,"longitude":101.68653,"admin1":"Kuala Lumpur","country":"Malaysia"}]}"#).unwrap();
        assert_eq!(v[0].name, "Kuala Lumpur, Malaysia");
        assert!(parse("{}").unwrap().is_empty());
        let fenix_proto::Msg::WeatherSet { lat_e4, lon_e4, .. } = v[0].to_msg() else { panic!() };
        assert_eq!((lat_e4, lon_e4), (31412, 1016865));
    }
}
