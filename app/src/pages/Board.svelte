<script lang="ts">
  // Artboard 11 (PcBoard): live mirror, facts and actions. The Logs row opens the log sheet
  // (artboard 16); "Reinstall firmware" opens the update dialog (artboard 12).
  import Icon from "../lib/Icon.svelte";
  import Mirror from "../lib/Mirror.svelte";
  import { linkText, rebootBoard, uptime, type Board } from "../lib/api";

  let {
    board,
    appVersion,
    streaming,
    onlogs,
    onreinstall,
  }: { board: Board; appVersion: string; streaming: boolean; onlogs: () => void; onreinstall: () => void } = $props();

  // The uptime ticks locally from the boot time the engine worked out at the handshake.
  let now = $state(Date.now());
  $effect(() => {
    const t = setInterval(() => (now = Date.now()), 30000);
    return () => clearInterval(t);
  });
  const up = $derived(Math.max(0, Math.floor((now - board.booted_at_ms) / 1000)));
  const matches = $derived(board.firmware === appVersion);
</script>

<div class="wrap">
  <h1>Board</h1>
  <div class="cols">
    <Mirror live={streaming} />
    <div class="side">
      <div class="group">
        <div class="row"><p>Connection</p><p class="value">{linkText(board.port, ", ")}</p></div>
        <div class="row"><p>Firmware</p><p class="value">fenixOS v{board.firmware}</p></div>
        <div class="row"><p>Model</p><p class="value">ESP32-2432S028R</p></div>
        <div class="row"><p>Up for</p><p class="value">{uptime(up)}</p></div>
      </div>
      <p class="hint">
        {#if matches}
          <Icon name="check-circle" size={14} color="var(--success)" />
          <span>Firmware matches this app</span>
        {:else}
          <Icon name="warning" size={14} color="var(--warning)" />
          <span>Firmware v{board.firmware} differs from this app (v{appVersion})</span>
        {/if}
      </p>
      <div class="group">
        <button type="button" class="row" onclick={() => rebootBoard()}>
          <Icon name="arrow-clockwise" size={18} color="var(--accent)" />
          <span>Reboot board</span>
        </button>
        <button type="button" class="row" onclick={onreinstall}>
          <Icon name="download-simple" size={18} color="var(--accent)" />
          <span>Reinstall firmware</span>
        </button>
        <button type="button" class="row" onclick={onlogs}>
          <Icon name="list-bullets" size={18} color="var(--accent)" />
          <span>Logs</span>
        </button>
      </div>
      <p class="hint plain">Shut down and restart are also on the board, in the Session app.</p>
    </div>
  </div>
</div>

<style>
  .wrap {
    max-width: 1000px;
    margin: 0 auto;
    display: flex;
    flex-direction: column;
    gap: 20px;
  }
  .cols {
    display: flex;
    flex-wrap: wrap;
    gap: 24px;
    align-items: flex-start;
  }
  .side {
    flex: 1 1 280px;
    max-width: 420px;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 20px;
  }
  .hint {
    margin: -8px 0 0 4px;
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 13px;
    line-height: 1.5;
    color: var(--label-2);
  }
</style>
