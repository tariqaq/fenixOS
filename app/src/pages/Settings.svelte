<script lang="ts">
  // Artboard 13 (PcSettings): Start with Windows, connection port, app updates, about.
  // Wireless (P32, P34; no artboard: the same groups and rows): the board's radio and Wi-Fi network.
  import { onDestroy } from "svelte";
  import Icon from "../lib/Icon.svelte";
  import LicensesSheet from "../lib/LicensesSheet.svelte";
  import Switch from "../lib/Switch.svelte";
  import {
    checkAppUpdate,
    getAppUpdate,
    getAutostart,
    getWeatherPlace,
    getWifiScan,
    installAppUpdate,
    listPorts,
    onAppUpdate,
    setAutostart,
    setPort,
    setRadio,
    setWeatherPlace,
    showWelcome,
    wifiJoin,
    wifiScan,
    weatherSearch,
    type AppUpdate,
    type PortItem,
    type Place,
    type RadioState,
    type WifiAp,
    type WifiScan,
  } from "../lib/api";
  import { menu as menuMotion } from "../lib/motion";

  let {
    appVersion,
    boardPort,
    radio,
  }: { appVersion: string; boardPort: string | null; radio: RadioState | null } = $props();

  let autostart = $state(false);
  getAutostart().then((v) => (autostart = v));

  async function toggle(on: boolean) {
    autostart = on;
    try {
      autostart = await setAutostart(on);
    } catch {
      autostart = !on;
    }
  }

  // Connection port
  let selected = $state<string | null>(null);
  let ports = $state<PortItem[]>([]);
  let menu = $state(false);
  const refresh = () =>
    listPorts().then((p) => {
      selected = p.selected;
      ports = p.ports;
    });
  refresh();
  const portName = (p: string) => (p.startsWith("tcp:") ? "Wi-Fi" : p);
  const portText = $derived(
    selected ? portName(selected) : boardPort ? `Automatic (${portName(boardPort)})` : "Automatic",
  );
  // Wi-Fi as a fixed port (P37): offered while the board runs Wi-Fi, or once picked.
  const WIFI_PORT = "tcp:fenixos.local";
  const wifiItem = $derived(selected === WIFI_PORT || radio?.mode === 2);

  function openMenu() {
    refresh();
    menu = !menu;
  }
  async function pick(p: string | null) {
    menu = false;
    if (p === selected) return;
    selected = p;
    await setPort(p);
  }

  // Updates
  let upd = $state<AppUpdate>({ stage: "idle", version: "", percent: 0, error: "" });
  getAppUpdate().then((u) => (upd = u));
  const stop = onAppUpdate((u) => (upd = u));
  onDestroy(() => stop.then((f) => f()));
  const updText = $derived.by(() => {
    switch (upd.stage) {
      case "checking":
        return `v${appVersion}, checking…`;
      case "current":
        return `v${appVersion}, up to date`;
      case "available":
        return `v${appVersion}, v${upd.version} available`;
      case "installing":
        return `Downloading v${upd.version}, ${upd.percent}%`;
      case "failed":
        // An install failure carries the version it tried; a failed check has none.
        return upd.version ? `v${appVersion}, could not install v${upd.version}` : `v${appVersion}, could not check`;
      default:
        return `v${appVersion}`;
    }
  });
  async function updateAction() {
    if (upd.stage === "available") await installAppUpdate().catch(() => {});
    else await checkAppUpdate();
  }

  // Wireless: one radio at a time; switching restarts the board (user 2026-10-30).
  const RADIOS = ["Off", "Bluetooth", "Wi-Fi"];
  const AUTH = ["Open", "WEP", "WPA", "WPA2", "WPA/WPA2", "Enterprise", "WPA3", "WPA2/WPA3", "Other"];
  let pendingRadio = $state<number | null>(null);
  const radioStatus = $derived.by(() => {
    if (!radio) return "";
    const ip = radio.ip.join(".");
    switch (radio.state) {
      case 1:
        return "Starting";
      case 2:
        return radio.mode === 1 ? "Discoverable as fenixOS" : "No network set";
      case 3:
        return `Connecting to ${radio.name}`;
      case 4:
        return radio.mode === 1 ? `Connected to ${radio.name}` : `${radio.name}, ${ip}`;
      case 5:
        return radio.mode === 2 ? `Cannot join ${radio.name}` : "Not working";
      default:
        return "";
    }
  });
  function pickRadio(m: number) {
    pendingRadio = radio && m !== radio.mode ? m : null;
  }
  async function switchRadio() {
    if (pendingRadio === null) return;
    await setRadio(pendingRadio);
    pendingRadio = null;
  }

  let scan = $state<WifiScan>({ scanning: false, from_pc: false, aps: [], error: "" });
  let timer: ReturnType<typeof setInterval> | undefined;
  async function startScan() {
    await wifiScan();
    scan = await getWifiScan();
    clearInterval(timer);
    timer = setInterval(async () => {
      scan = await getWifiScan();
      if (!scan.scanning) clearInterval(timer);
    }, 500);
  }
  onDestroy(() => clearInterval(timer));
  getWifiScan().then((s) => (scan = s));

  let joining = $state<string | null>(null); // SSID whose password row is open; "" = other network
  let ssid = $state("");
  let password = $state("");
  function openJoin(ap: WifiAp | null) {
    joining = ap ? ap.ssid : "";
    ssid = ap ? ap.ssid : "";
    password = "";
  }
  async function join() {
    if (!ssid.trim()) return;
    await wifiJoin(ssid.trim(), password);
    joining = null;
    password = "";
  }
  const bars = (rssi: number) => (rssi >= -60 ? "wifi-high" : rssi >= -75 ? "wifi-medium" : "wifi-low");

  // Weather (P34): the board fetches it in Wi-Fi mode for this place.
  let place = $state<Place | null>(null);
  getWeatherPlace().then((p) => (place = p));
  let query = $state("");
  let places = $state<Place[]>([]);
  let searching = $state(false);
  let searchError = $state("");
  async function findPlaces() {
    if (!query.trim()) return;
    searching = true;
    searchError = "";
    try {
      places = await weatherSearch(query);
      if (!places.length) searchError = `No place called "${query.trim()}"`;
    } catch (e) {
      searchError = String(e);
    }
    searching = false;
  }
  async function pickPlace(p: Place | null) {
    await setWeatherPlace(p);
    place = p;
    places = [];
    query = "";
  }

  let licenses = $state(false);

  function outside(e: MouseEvent) {
    if (menu && !(e.target as HTMLElement).closest(".port")) menu = false;
  }
  function key(e: KeyboardEvent) {
    if (e.key === "Escape") menu = false;
  }
</script>

<svelte:window onclick={outside} onkeydown={key} />

<div class="wrap">
  <h1>Settings</h1>
  <h2 class="section-title">General</h2>
  <div class="group">
    <div class="row">
      <p id="lbl-autostart">Start with Windows</p>
      <span class="push"><Switch checked={autostart} labelledby="lbl-autostart" onchange={toggle} /></span>
    </div>
  </div>
  <p class="note">
    Closing the window keeps fenixOS in the tray. Quit from the tray menu. If an app needs administrator rights for an
    action, Windows asks you at that moment.
  </p>

  <h2 class="section-title">Board</h2>
  <div class="port">
    <div class="group">
      <button type="button" class="row plain" aria-haspopup="menu" aria-expanded={menu} onclick={openMenu}>
        <span>Connection port</span>
        <span class="value">{portText}</span>
        <Icon name="caret-up-down" size={14} color="var(--label-2)" />
      </button>
    </div>
    {#if menu}
      <div class="menu" role="menu" aria-label="Connection port" transition:menuMotion>
        <button type="button" role="menuitemradio" aria-checked={selected === null} onclick={() => pick(null)}>
          <span class="tick">{#if selected === null}<Icon name="check" size={14} color="var(--accent)" />{/if}</span>
          <span>Automatic</span>
        </button>
        {#if ports.length}<div class="sep"></div>{/if}
        {#each ports as p (p.name)}
          <button type="button" role="menuitemradio" aria-checked={selected === p.name} onclick={() => pick(p.name)}>
            <span class="tick">{#if selected === p.name}<Icon name="check" size={14} color="var(--accent)" />{/if}</span>
            <span>{p.name}</span>
            <span class="sub">{p.board ? "CH340, board chip" : p.product}</span>
          </button>
        {/each}
        {#if wifiItem}
          <button type="button" role="menuitemradio" aria-checked={selected === WIFI_PORT} onclick={() => pick(WIFI_PORT)}>
            <span class="tick">{#if selected === WIFI_PORT}<Icon name="check" size={14} color="var(--accent)" />{/if}</span>
            <span>Wi-Fi</span>
            <span class="sub">fenixos.local, even with USB plugged in</span>
          </button>
        {/if}
        {#if selected && selected !== WIFI_PORT && !ports.some((p) => p.name === selected)}
          <button type="button" role="menuitemradio" aria-checked="true" onclick={() => pick(selected)}>
            <span class="tick"><Icon name="check" size={14} color="var(--accent)" /></span>
            <span>{selected}</span>
            <span class="sub">not plugged in</span>
          </button>
        {/if}
      </div>
    {/if}
  </div>
  <p class="note">
    Automatic picks the first port with a CH340 chip that answers as a fenixOS board, or the board on Wi-Fi when
    none is plugged in.
  </p>

  <h2 class="section-title">Wireless</h2>
  {#if !radio}
    <div class="group">
      <div class="row">
        <p>Board radio</p>
        <p class="value">{boardPort ? "Needs board firmware 0.1.8 or later" : "Board not connected"}</p>
      </div>
    </div>
  {:else}
    <div class="group" role="radiogroup" aria-label="Board radio">
      {#each RADIOS as name, i (name)}
        <button type="button" class="row plain" role="radio" aria-checked={radio.mode === i} onclick={() => pickRadio(i)}>
          <span class="tick">{#if radio.mode === i}<Icon name="check" size={14} color="var(--accent)" />{/if}</span>
          <span>{name}</span>
          {#if radio.mode === i && radioStatus}<span class="value">{radioStatus}</span>{/if}
        </button>
      {/each}
    </div>
    {#if pendingRadio !== null}
      <div class="group confirm">
        <p>
          The board restarts to {pendingRadio === 0 ? `turn ${RADIOS[radio.mode]} off` : `switch to ${RADIOS[pendingRadio]}`}.
          {#if radio.mode !== 0 && pendingRadio !== 0}Only one radio runs at a time, so {RADIOS[radio.mode]} turns off.{/if}
        </p>
        <div class="actions">
          <button type="button" class="secondary" onclick={() => (pendingRadio = null)}>Cancel</button>
          <button type="button" class="primary" onclick={switchRadio}>Restart board</button>
        </div>
      </div>
    {/if}
  {/if}
  <p class="note">One radio runs at a time. Switching restarts the board.</p>

  <h2 class="section-title">Wi-Fi network</h2>
  <div class="group">
    <div class="row">
      <p>Networks</p>
      <button type="button" class="action push" disabled={scan.scanning} onclick={startScan}>
        {scan.scanning ? "Scanning…" : scan.aps.length ? "Scan again" : "Scan"}
      </button>
    </div>
    {#each scan.aps as ap (ap.ssid)}
      <button type="button" class="row plain" onclick={() => openJoin(ap)}>
        <Icon name={bars(ap.rssi)} size={16} color="var(--label)" />
        <span>{ap.ssid}</span>
        {#if radio?.mode === 2 && radio.name === ap.ssid && radio.state === 4}<Icon name="check" size={14} color="var(--accent)" />{/if}
        <span class="value">{AUTH[ap.auth] ?? "Other"}, {ap.rssi} dBm</span>
        {#if ap.auth !== 0}<Icon name="lock" size={14} color="var(--label-2)" />{/if}
      </button>
      {#if joining === ap.ssid}
        <div class="row join">
          {#if ap.auth === 5}
            <p class="value">Enterprise networks need a user name; fenixOS cannot join them.</p>
          {:else}
            {#if ap.auth !== 0}
              <input type="password" placeholder="Password" maxlength="64" bind:value={password} aria-label="Password for {ap.ssid}" />
            {/if}
            <button type="button" class="action" disabled={!boardPort} onclick={join}>Join</button>
          {/if}
        </div>
      {/if}
    {/each}
    <button type="button" class="row" onclick={() => openJoin(null)}>Other network…</button>
    {#if joining === ""}
      <div class="row join">
        <input type="text" placeholder="Network name" maxlength="32" bind:value={ssid} aria-label="Network name" />
        <input type="password" placeholder="Password" maxlength="64" bind:value={password} aria-label="Password" />
        <button type="button" class="action" disabled={!boardPort || !ssid.trim()} onclick={join}>Join</button>
      </div>
    {/if}
    {#if radio?.mode === 2 && radio.name}
      <button type="button" class="row danger" onclick={() => wifiJoin("", "")}>Forget {radio.name}</button>
    {/if}
  </div>
  {#if scan.error}<p class="note warn">{scan.error}</p>{/if}
  <p class="note">
    {scan.from_pc
      ? "Listed by this PC. Switch the board to Wi-Fi to see what it hears. "
      : ""}2.4 GHz networks only. The board saves the network in plain text and joins it in Wi-Fi mode.
  </p>

  <h2 class="section-title">Weather</h2>
  <div class="group">
    <div class="row">
      <p>Place</p>
      <p class="value">{place ? place.name : "Off"}</p>
    </div>
    <form class="row join" onsubmit={(e) => (e.preventDefault(), findPlaces())}>
      <input type="text" placeholder="Search for a city" maxlength="60" bind:value={query} aria-label="City" />
      <button type="submit" class="action" disabled={searching || !query.trim()}>{searching ? "Searching…" : "Search"}</button>
    </form>
    {#each places as p (p.name + p.lat + p.lon)}
      <button type="button" class="row plain" onclick={() => pickPlace(p)}>
        <span>{p.name}</span>
        {#if place && place.lat === p.lat && place.lon === p.lon}<Icon name="check" size={14} color="var(--accent)" />{/if}
      </button>
    {/each}
    {#if place}
      <button type="button" class="row danger" onclick={() => pickPlace(null)}>Turn weather off</button>
    {/if}
  </div>
  {#if searchError}<p class="note warn">{searchError}</p>{/if}
  <p class="note">The board shows the weather on Home in Wi-Fi mode. It asks Open-Meteo every 15 minutes, with no account or key.</p>

  <h2 class="section-title">Updates</h2>
  <div class="group">
    <div class="row"><p>fenixOS</p><p class="value">{updText}</p></div>
    <button
      type="button"
      class="row"
      disabled={upd.stage === "checking" || upd.stage === "installing"}
      onclick={updateAction}
    >
      {upd.stage === "available" ? `Install v${upd.version} and restart` : "Check for updates"}
    </button>
  </div>
  {#if upd.stage === "failed" && upd.error}<p class="note warn">{upd.error}</p>{/if}
  <p class="note">The app and the board firmware share one version. After the app updates, it offers to update the board.</p>

  <h2 class="section-title">About</h2>
  <div class="group">
    <button type="button" class="row" onclick={() => showWelcome()}>Show welcome again</button>
    <a class="row" href="https://github.com/tariqaq/fenixOS">Source code on GitHub</a>
    <button type="button" class="row" onclick={() => (licenses = true)}>Open-source licenses</button>
  </div>
</div>

{#if licenses}
  <LicensesSheet onclose={() => (licenses = false)} />
{/if}

<style>
  .wrap {
    max-width: 640px;
    margin: 0 auto;
    display: flex;
    flex-direction: column;
    gap: 8px;
  }
  h1 {
    margin-bottom: 16px;
  }
  .row {
    padding-top: 8px;
    padding-bottom: 8px;
  }
  .push {
    margin-left: auto;
  }
  .plain {
    color: var(--label);
    gap: 8px;
  }
  button.row:disabled {
    color: var(--label-2);
  }
  .port {
    position: relative;
  }
  .menu {
    position: absolute;
    right: 8px;
    top: calc(100% + 4px);
    min-width: 280px;
    background: var(--surface-2);
    border-radius: 12px;
    padding: 6px;
    box-shadow: 0 16px 48px rgba(0, 0, 0, 0.6);
    z-index: 10;
    transform-origin: top right;
    display: flex;
    flex-direction: column;
  }
  .menu button {
    height: 36px;
    border: 0;
    border-radius: 8px;
    background: transparent;
    color: var(--label);
    font-size: 15px;
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 0 10px 0 6px;
    text-align: left;
  }
  .menu button:hover {
    background: var(--surface-3);
    filter: none;
  }
  .menu button:active {
    transform: none;
  }
  .tick {
    width: 16px;
    display: flex;
    justify-content: center;
  }
  .sub {
    margin-left: auto;
    padding-left: 16px;
    font-size: 13px;
    color: var(--label-2);
  }
  .sep {
    height: 1px;
    background: var(--separator);
    margin: 4px 6px;
  }
  .confirm {
    padding: 14px 16px;
    display: flex;
    flex-direction: column;
    gap: 12px;
    font-size: 15px;
  }
  .actions {
    display: flex;
    justify-content: flex-end;
    gap: 8px;
  }
  .primary,
  .secondary {
    height: 32px;
    padding: 0 14px;
    border: 0;
    border-radius: 8px;
    font-size: 15px;
  }
  .primary {
    background: var(--accent-fill);
    color: #fff;
    font-weight: 600;
  }
  .secondary {
    background: transparent;
    color: var(--accent);
  }
  .action {
    border: 0;
    background: transparent;
    padding: 0;
    color: var(--accent);
    font-size: 15px;
  }
  .action:disabled {
    color: var(--label-2);
  }
  .join {
    gap: 8px;
  }
  .join input {
    flex: 1;
    min-width: 0;
    height: 30px;
    padding: 0 10px;
    border: 0;
    border-radius: 8px;
    background: var(--surface-2);
    color: var(--label);
    font: inherit;
    font-size: 15px;
  }
  .danger {
    color: var(--danger);
  }
  .warn {
    color: var(--warning);
    margin-bottom: 0;
    overflow-wrap: anywhere;
  }
</style>
