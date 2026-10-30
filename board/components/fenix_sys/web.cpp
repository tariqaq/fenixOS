// Phone web page (P34): http://fenixos.local/ in Wi-Fi mode. Status only: the page is reachable
// by anyone on the network without a password (user choice: no encryption), so it changes
// nothing on the board. One small page in the design language (tokens from design/tokens.json),
// refreshed from /status.json every 2 s.
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "fenix_proto.h"
#include "fenix_sys.h"

namespace fx::sys {

static const char PAGE[] = R"html(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark"><title>fenixOS</title><style>
:root{--bg:#000;--s1:#1C1C1E;--sep:#38383A;--l:#fff;--l2:#98989F;--ok:#30D158;--warn:#FF9F0A}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--l);
font:15px/1.35 -apple-system,BlinkMacSystemFont,"Segoe UI",Inter,sans-serif;padding:24px 16px;max-width:520px;margin:auto}
h1{font-size:28px;margin:0}#clock{font-size:56px;font-weight:600;letter-spacing:-1px;margin:16px 0 0}
#date{color:var(--l2);margin:0 0 24px}h2{font-size:13px;font-weight:400;color:var(--l2);text-transform:uppercase;
letter-spacing:.02em;margin:24px 16px 8px}.g{background:var(--s1);border-radius:14px;overflow:hidden}
.r{display:flex;justify-content:space-between;gap:12px;padding:12px 16px;border-bottom:1px solid var(--sep)}
.r:last-child{border:0}.v{color:var(--l2);text-align:right}.ok{color:var(--ok)}.warn{color:var(--warn)}
p.n{color:var(--l2);font-size:13px;margin:8px 16px}</style></head><body>
<h1>fenixOS</h1><p id="clock">--:--</p><p id="date">Loading</p>
<h2>Board</h2><div class="g" id="board"></div>
<h2>Wi-Fi</h2><div class="g" id="wifi"></div>
<p class="n">This page only shows the board's state. Change settings on the board or in fenixOS on your PC.</p>
<script>
const rows=(id,a)=>document.getElementById(id).innerHTML=a.map(([k,v,c])=>
`<div class="r"><span>${k}</span><span class="v ${c||""}">${v}</span></div>`).join("");
const up=s=>{const d=Math.floor(s/86400),h=Math.floor(s/3600)%24,m=Math.floor(s/60)%60;
return d?`${d} d ${h} h`:h?`${h} h ${m} min`:`${m} min`};
async function tick(){try{const j=await(await fetch("/status.json",{cache:"no-store"})).json();
document.getElementById("clock").textContent=j.time||"--:--";
document.getElementById("date").textContent=j.date?j.date+(j.weather?" · "+j.weather:""):"Time not set yet";
rows("board",[["Version",j.version],["Running for",up(j.uptime)],["PC",j.pc?"Connected":"Not connected",j.pc?"ok":"warn"],
["Free memory",Math.round(j.heap/1024)+" KB"],["Apps",j.apps+" KB used"]]);
rows("wifi",[["Network",j.ssid],["Signal",j.rssi+" dBm"],["Address",j.ip]]);}catch(e){
document.getElementById("date").textContent="Board not answering"}}
tick();setInterval(tick,2000);
</script></body></html>)html";

static esp_err_t page(httpd_req_t* r) {
  httpd_resp_set_type(r, "text/html; charset=utf-8");
  return httpd_resp_send(r, PAGE, sizeof PAGE - 1);
}

static const char* const DAYS[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char* const MONTHS[] = {"January", "February", "March",     "April",   "May",      "June",
                                     "July",    "August",   "September", "October", "November", "December"};

static esp_err_t status(httpd_req_t* r) {
  char t[8] = "", d[40] = "", wx[48] = "";
  struct tm tm;
  if (clock_local(&tm)) {
    snprintf(t, sizeof t, "%02d:%02d", tm.tm_hour, tm.tm_min);
    snprintf(d, sizeof d, "%s, %d %s", DAYS[tm.tm_wday], tm.tm_mday, MONTHS[tm.tm_mon]);
  }
  Weather w;
  if (weather(&w)) {
    int deg = (w.temp_x10 + (w.temp_x10 < 0 ? -5 : 5)) / 10;
    snprintf(wx, sizeof wx, "%d\xC2\xB0 %s", deg, weather_text(w.code));
  }
  RadioInfo ri = radio_info();
  uint32_t used = 0, total = 0;
  store_usage(&used, &total);
  // The SSID is escaped for JSON: quotes and backslashes only (control bytes are dropped).
  char ssid[2 * fnx::SSID_LEN];
  size_t k = 0;
  for (const char* p = ri.name; *p && k < sizeof ssid - 2; p++) {
    if ((uint8_t)*p < 0x20) continue;
    if (*p == '"' || *p == '\\') ssid[k++] = '\\';
    ssid[k++] = *p;
  }
  ssid[k] = 0;
  char b[512];
  int n = snprintf(b, sizeof b,
                   "{\"version\":\"%s\",\"uptime\":%lu,\"heap\":%u,\"apps\":%lu,\"pc\":%s,\"time\":\"%s\","
                   "\"date\":\"%s\",\"weather\":\"%s\",\"ssid\":\"%s\",\"rssi\":%d,\"ip\":\"%u.%u.%u.%u\"}",
                   esp_app_get_description()->version, (unsigned long)(esp_timer_get_time() / 1000000),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned long)used,
                   link_connected() ? "true" : "false", t, d, wx, ssid, ri.rssi, ri.ip[0], ri.ip[1], ri.ip[2],
                   ri.ip[3]);
  httpd_resp_set_type(r, "application/json");
  return httpd_resp_send(r, b, n);
}

void web_start() {
  httpd_config_t c = HTTPD_DEFAULT_CONFIG();
  c.max_open_sockets = 3;  // a phone or two; each socket costs lwIP buffers
  c.lru_purge_enable = true;
  c.core_id = 0;           // the UI keeps core 1
  c.stack_size = 4096;
  httpd_handle_t h = nullptr;
  if (httpd_start(&h, &c) != ESP_OK) return;
  httpd_uri_t u = {};
  u.method = HTTP_GET;
  u.uri = "/";
  u.handler = page;
  httpd_register_uri_handler(h, &u);
  u.uri = "/status.json";
  u.handler = status;
  httpd_register_uri_handler(h, &u);
}

}  // namespace fx::sys
