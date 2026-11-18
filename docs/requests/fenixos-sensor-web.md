# Request: sensor read time and app web pages (fenixOS v0.1.31)

Written for the fenixOS repo (`tariqaq/fenixOS`) and the agent working in it. It follows `docs/requests/fenixos-sensor-support.md` (built in v0.1.29 and v0.1.30). The requesting app is again `io.github.tariqaq.sensors` (repo `tariqaq/sensors`).

The design below is a proposal. Where it conflicts with how fenixOS is built, keep the goals in section 1 and adapt the mechanism. Document what was built and where it differs, as a status block at the top of this file like the last request.

> **Status: implemented in fenixOS v0.1.31 (P61), 2026-11-18.** Pin `fenix-sdk` and `fenix-pkg` to `v0.1.31` and set `min_fenixos = "0.1.31"` to use `<b>_time`. Reference: `docs/packages.md` "Sensors", `docs/sd-card.md` "Status page from the card", `docs/sidecars.md` (`file_put`), and the template's `docs/fenixos/app-format.md` "App web page" / `dev-guide.md` section 6 (re-copy them). Tested on the real board with both DHT22s over Wi-Fi. Built: 3.1 (`_time`), 3.2 (`/sensors.json`), 3.3 (10 s watch), 3.4 (app web folders) and the optional `/apps.json` with links on the built-in page. Where it differs:
>
> - `_time` is generated for every sensor once `min_fenixos` is 0.1.31 or newer (used in the layout or not), and refused in a layout with an older `min_fenixos`; 0.1.29 packages keep six bindings. `<b>_age` not built.
> - `/sensors.json` is as in 3.2. After a pause in polling, a pin shows `"state": "reading"` (with its last good values, `read` and `age`) for up to 2 s, until the first read.
> - `/apps/<id>` (no slash) redirects to `/apps/<id>/`.
> - Not built (optional): the URL on the PC app's app page (3.5), `fenix_sdk::web_url`, `fenix web`. `fenix files put page.html data/<id>/web/index.html` works as is.
> - **Criterion 7 not met as written:** with the app open, three clients polling every 1 s and a Wi-Fi link session, the heap low point was 25.6 KB (27.7 KB without the link session), not above 35.9 KB. The 35.9 KB was measured on Home; an open app's layout and the sensor task (3.8 KB) take the difference, and the three sockets cost the same 12 to 17 KB as then. 25.6 KB is within lows the board runs at (Wi-Fi stream 23.9 KB), no request failed beyond 1 of ~210, readings stayed at most 2 s old. UI stutter not checked by eye. If it matters, poll every 2 s from fewer clients.

## 1. Goals

1. **Last updated time on the board.** Each sensor card shows when its value was last read, with seconds (`14:03:27`). It must be right with or without the PC. Today an app can't do this: the board sets only the six sensor bindings, and a PC-set time freezes while the PC is away even though the readings stay live.
2. **A web page per app, served by the board.** Any device on the board's Wi-Fi network opens a page (for example `http://fenixos.local/apps/io.github.tariqaq.sensors/`) that shows the live readings. The app ships the page itself; fenixOS only serves it and the data.
3. **Live sensor data for that page,** whether or not the app is open on the board and whether or not the PC is connected.

## 2. What exists today (v0.1.30, from the source)

- `board/components/fenix_sys/web.cpp`:
  - In Wi-Fi mode, `esp_http_server` on port 80: 3 sockets, LRU purge, core 0, 3 KB stack, wildcard matching.
  - `/status.json` is built in (`version uptime heap apps pc time date weather ssid rssi ip`; `time` is `HH:MM`).
  - `/*` serves `/sd/fenixos/web/` first (`from_card`, 1 KB chunks, `Cache-Control: no-cache`, `..` `\` `%` refused), and the built-in page for `/`.
  - GET only.
- `web/` is one folder for the whole board. Only the PC app (`FILE_WRITE_*`) or `fenix files put` writes there. A PC half's `put_file` writes only to `/sd/fenixos/data/<id>/` (≤ 32 KB per file, `name` may hold `/`), and that folder is not served.
- `sensors.cpp`: a slot is read while its app is open, while its PC half runs with the link up, or while a `SENSOR_WATCH` asks. The UI decides every 0.5 s (`sensor_want_tick`).
- `clock_local(&tm)` gives local time once SNTP or the PC has set the clock.

## 3. Board changes

### 3.1 Seventh sensor binding: `<b>_time`

| Binding | Type | Value |
|---|---|---|
| `<b>_time` | text | Local time of the last **good** read, `HH:MM:SS` (24 h, the clock fonts can draw it). `--:--:--` before the first good read or while the clock is not set |

- Set it on every good read, even when temperature and humidity didn't change. The point is to show the reading is fresh, so this binding changes every 2 s while the sensor is live.
- Keep the last good time while the state is `Not connected` or `Read error`, so the card shows when it last had data.
- If the clock is set later (SNTP after boot), the next good read gives a real time.
- `fenix-pkg build`:
  - Accept `<b>_time` as the seventh name under a sensor prefix.
  - Require `min_fenixos` ≥ 0.1.31 when the layout uses it. An older board would leave it at the placeholder forever, so give the same kind of error as for `view`/`show`.
  - List it as board-set in `inspect`. It counts toward the 64 bindings.
- Board side: like the other six, not saved to the card's last values.
- Optional, only if it is cheap: `<b>_age` int, seconds since the last good read. Not needed by this app.

### 3.2 `/sensors.json`

A new built-in endpoint, registered before `/*` like `/status.json`:

```json
{
  "time": "14:03:28",
  "pc": true,
  "sensors": [
    { "pin": 22, "kind": "dht22", "state": "ok", "label": "Live",
      "temp": 23.4, "hum": 45.2, "read": "14:03:27", "age": 1,
      "apps": [{ "id": "io.github.tariqaq.sensors", "bind": "s1" }] },
    { "pin": 27, "kind": "dht22", "state": "missing", "label": "Not connected",
      "temp": 22.9, "hum": 50.1, "read": "14:01:02", "age": 146,
      "apps": [{ "id": "io.github.tariqaq.sensors", "bind": "s2" }] }
  ]
}
```

- `time`: board local time with seconds (`""` while the clock is not set). `pc`: link up.
- One entry per **declared** pin (any installed app has a `[[sensor]]` on it). Undeclared pins are left out.
- `state` (`reading` `ok` `missing` `error`) and `label` (the same text as `<b>_state`).
- `temp` and `hum` are the last good values, `null` before the first good read. A page checks `state` to know whether they are live.
- `read`: `HH:MM:SS` of the last good read (`""` if none or no clock). `age`: seconds since it (`null` if none). The board's uptime clock gives the age, so it's right even without a set clock.
- `apps`: which apps declare the pin, with their prefix, so a page can find its own sensors.
- No `?app=` filter needed: there are only two pins.
- Small reply (a few hundred bytes). Build it in a stack buffer like `status_json`, sized for two pins with an app list.

### 3.3 Keep reading while a page is watching

- Each `/sensors.json` request marks every declared pin as wanted for the next **10 s**.
- `sensor_want_tick` treats that mark as one more reason to read, beside app open, PC half running and `SENSOR_WATCH`.
- When the page closes, reads stop again after 10 s.
- Readings caused only by the page do not send `SENSOR` frames to the PC. Keep the current rule: frames go out only for a running PC half or a watch.

### 3.4 App web folders

Serve each app's own web folder:

| URL | File |
|---|---|
| `/apps/<id>/` | `/sd/fenixos/data/<id>/web/index.html` |
| `/apps/<id>/<path>` | `/sd/fenixos/data/<id>/web/<path>` |

- Only the `web/` subfolder of an app's data folder is served. Other files in `data/<id>/` (an app's own state) stay private.
- The same path rules as `from_card`:
  - `..`, `\` and `%` are refused.
  - `<id>` must be an installed app's id.
  - A path ending in `/` means its `index.html`.
  - Same types, 1 KB chunks, `Cache-Control: no-cache`, GET only.
- Missing file or no card: 404.
- The PC half writes the files with the existing `put_file("web/index.html", …)`. No protocol change: `name` may already hold `/`. The 32 KB per-file cap stays; apps split their page into several files.
- Removing the app deletes the folder (it already does), so the page goes with the app.
- Optional, for discovery:
  - `/apps.json` lists the installed apps that have `web/index.html`: `[{id, name, color}]`.
  - The built-in status page (and a card `web/index.html`, through `/apps.json`) shows them as links.
- **Security note:** anyone on the network can GET these pages, as with `web/` today. Document that an app must put only public things in `web/`: no settings, no tokens. The PC app's permission text for `sensors` or `files` doesn't change, but the docs for `put_file` should say `web/` is served on the network.

### 3.5 Board screen hint (optional)

To help users find the page, optionally:

- show the board's address on the app's settings page in the PC app (`http://fenixos.local/apps/<id>/`) when the app has `web/index.html` on the board and the board is in Wi-Fi mode;
- or let the app bind its URL text itself. The PC half can already `set` a label, so nothing is needed from fenixOS for this.

## 4. Package, SDK and PC app

- `fenix-pkg`: `<b>_time` (3.1). No package format change: the binding list is already generated from the sensor table.
- `fenix-sdk`: nothing required. Optional: `fenix_sdk::web_url(app)` or a field from `hello` with the board's address, if the PC app knows it. Optional: `put_file` docs note that `web/` is public.
- PC app: optional, show the page URL on the app page (3.5).
- No new permission. The page is part of the app, and its files go through `put_file` as today.

## 5. `fenix` CLI

- `fenix files put page.html data/<id>/web/index.html` should work for testing app pages without a PC half. Today PC writes reach only `fonts/ data/ web/`, and `data/` is already allowed, so this may need nothing.
- Optional: `fenix web` prints the board's page URLs and fetches `/sensors.json` once.

## 6. Docs to update in fenixOS

- `docs/packages.md` "Sensors": `<b>_time`, `min_fenixos` 0.1.31.
- `docs/sd-card.md` "Status page from the card": `/sensors.json`, `/apps/<id>/`, the 10 s watch, `/apps.json` if built.
- `docs/sidecars.md`: `put_file` into `web/` makes a public page at `/apps/<id>/`.
- `docs/board-info.md`: the measured heap and CPU cost with a page polling every 1 s from 1 and 3 clients.
- The template copies under `templates/app/docs/fenixos/` (`app-format.md`, `pc-half.md`, `dev-guide.md`), then rebuild `fenixos-app-template.zip`.

## 7. What the app will do with it (for context)

- **Board layout:** each sensor card gets a caption line, "Updated 14:03:27", bound to `s1_time` / `s2_time`. `min_fenixos` goes to 0.1.31.
- **PC half:** on board connect (and when names or the page version change), uploads the page to `data/<id>/web/`:
  - `index.html`, `app.css`, `app.js` (each ≤ 32 KB);
  - `config.json` with the sensor names from settings;
  - `history.json`, rewritten every 5 min while connected, with 5-minute averages for the last 24 h per sensor (≤ 32 KB).
  - The page reads it for a chart. With the PC away, the chart keeps the last uploaded history and the live values keep coming from `/sensors.json`.
- **Page:**
  - Dark, in the fenixOS design language (tokens, Inter, Phosphor icons).
  - Polls `/sensors.json` every 1 s and shows each sensor's temperature, humidity, state and "Updated" time.
  - A three.js 3D model of the board with its two sensors, tinted by temperature, loaded from a pinned CDN version. It falls back to a flat view when the CDN can't be reached.
  - A 24 h chart from `history.json`, plus the points seen while the page is open.
- Needs Wi-Fi mode and an SD card. In USB-only mode or without a card the page isn't there, and the app works as in phase 1.

## 8. Acceptance criteria

1. A layout with `s1_time` shows `HH:MM:SS` of each good read, advancing every 2 s, with the PC connected and with the PC away. It shows `--:--:--` before the clock is set or before the first good read, and holds the last good time while the sensor is unplugged.
2. `fenix-pkg build` refuses `s1_time` with `min_fenixos` below 0.1.31 and accepts it at 0.1.31. `inspect` lists seven board-set bindings per sensor.
3. `GET /sensors.json` returns the shape in 3.2 for the declared pins only, with `age` correct without a set clock.
4. With the app closed on the board and no PC half running, polling `/sensors.json` every 1 s gives fresh readings (age ≤ 3 s). Reads stop ~10 s after polling stops (check with `fenix sysinfo` or the log). No `SENSOR` frames are sent for page-only reads.
5. `put_file("web/index.html", …)` from a PC half makes `http://fenixos.local/apps/<id>/` serve it. Sibling files serve by path. `data/<id>/count.txt` (outside `web/`) gets 404, and so do `..`, `%`, `\` and unknown ids.
6. Removing the app removes the page (404).
7. Heap: with the app open, a Wi-Fi link session and 3 clients polling `/sensors.json` every 1 s, the board stays above the safe low point measured for the status page (35.9 KB, `docs/sd-card.md`), with no UI stutter. Record the numbers.
8. Docs and template copies are updated, and v0.1.31 is tagged so the app can pin `fenix-sdk` and `fenix-pkg` to it.

## 9. Split if needed

The parts are independent and can ship separately in this order of value:

1. `<b>_time` (3.1).
2. `/sensors.json` plus the 10 s watch (3.2, 3.3).
3. App web folders (3.4).

Without 3, the app could still ship a page that the user copies to `web/` by hand. That would replace the board's status page, so it's only a stopgap.
