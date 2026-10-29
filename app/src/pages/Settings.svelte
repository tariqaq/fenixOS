<script lang="ts">
  // Artboard 13 (PcSettings). P7: Start with Windows and About; the connection port, updates and
  // "Show welcome again" rows come with P8 / P9.
  import Switch from "../lib/Switch.svelte";
  import { getAutostart, setAutostart } from "../lib/api";

  let { appVersion }: { appVersion: string } = $props();
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
</script>

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
  <h2 class="section-title">About</h2>
  <div class="group">
    <div class="row"><p>fenixOS</p><p class="value">v{appVersion}</p></div>
    <a class="row" href="https://github.com/tariqaq/fenixOS">Source code on GitHub</a>
  </div>
</div>

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
</style>
