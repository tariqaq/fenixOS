<script lang="ts">
  // Artboard 11 (PcBoard): live mirror, facts and actions. The Logs row opens the log sheet
  // (artboard 16); "Reinstall firmware" opens the update dialog (artboard 12). "Screenshot to the
  // card" (P53) has the board save its screen on its SD card; the app keeps a copy.
  import Icon from "../lib/Icon.svelte";
  import Mirror from "../lib/Mirror.svelte";
  import { boardShot, linkText, openShotFolder, rebootBoard, sdText, uptime, type Board } from "../lib/api";

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

  let shooting = $state(false);
  let shotNote = $state("");
  let shotOk = $state(false);
  async function shoot() {
    shooting = true;
    shotNote = "Saving…";
    try {
      const s = await boardShot();
      shotOk = true;
      shotNote = `Saved as ${s.card} on the card, with a copy on this PC.`;
    } catch (e) {
      shotOk = false;
      shotNote = `${e}`;
    }
    shooting = false;
  }
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
        {#if board.sd}
          <div class="row"><p>SD card</p><p class="value">{sdText(board.sd)}</p></div>
        {/if}
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
        {#if board.sd?.state === 1}
          <button type="button" class="row" disabled={shooting} onclick={shoot}>
            <Icon name="camera" size={18} color="var(--accent)" />
            <span>Screenshot to the card</span>
          </button>
        {/if}
      </div>
      {#if shotNote}
        <p class="hint plain" role="status">
          <span>{shotNote}</span>
          {#if shotOk}
            <button type="button" class="link" onclick={() => openShotFolder()}>Show</button>
          {/if}
        </p>
      {/if}
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
  .link {
    border: 0;
    background: none;
    padding: 0;
    font: inherit;
    color: var(--accent);
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
