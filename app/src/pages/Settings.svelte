<script lang="ts">
  // Artboard 13 (PcSettings): Start with Windows, connection port, app updates, about.
  // "Show welcome again" joins About with the first-run experience (P9).
  import { onDestroy } from "svelte";
  import Icon from "../lib/Icon.svelte";
  import LicensesSheet from "../lib/LicensesSheet.svelte";
  import Switch from "../lib/Switch.svelte";
  import {
    checkAppUpdate,
    getAppUpdate,
    getAutostart,
    installAppUpdate,
    listPorts,
    onAppUpdate,
    setAutostart,
    setPort,
    type AppUpdate,
    type PortItem,
  } from "../lib/api";

  let { appVersion, boardPort }: { appVersion: string; boardPort: string | null } = $props();

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
  const portText = $derived(selected ?? (boardPort ? `Automatic (${boardPort})` : "Automatic"));

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
        return `v${appVersion}, could not check`;
      default:
        return `v${appVersion}`;
    }
  });
  async function updateAction() {
    if (upd.stage === "available") await installAppUpdate().catch(() => {});
    else await checkAppUpdate();
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
      <div class="menu" role="menu" aria-label="Connection port">
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
        {#if selected && !ports.some((p) => p.name === selected)}
          <button type="button" role="menuitemradio" aria-checked="true" onclick={() => pick(selected)}>
            <span class="tick"><Icon name="check" size={14} color="var(--accent)" /></span>
            <span>{selected}</span>
            <span class="sub">not plugged in</span>
          </button>
        {/if}
      </div>
    {/if}
  </div>
  <p class="note">Automatic picks the first port with a CH340 chip that answers as a fenixOS board.</p>

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
  .warn {
    color: var(--warning);
    margin-bottom: 0;
    overflow-wrap: anywhere;
  }
</style>
