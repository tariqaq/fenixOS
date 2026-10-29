<script lang="ts">
  // Artboard 12 (PcUpdate): version mismatch on connect, Reinstall firmware from the Board page,
  // and the progress of either. The engine does the work; this only shows `State.update`.
  import { FAST, scrim, sheet } from "./motion";
  import Icon from "./Icon.svelte";
  import { clearUpdate, updateBoard, type State } from "./api";

  let {
    snap,
    mode,
    onclose,
  }: { snap: State; mode: "mismatch" | "reinstall" | "progress"; onclose: () => void } = $props();

  let error = $state("");
  const u = $derived(snap.update);
  const board = $derived(snap.board?.firmware ?? "");
  const app = $derived(snap.app_version);
  const bundled = $derived(snap.bundled_firmware);
  const newer = $derived(board !== "" && cmp(board, app) > 0);

  function cmp(a: string, b: string): number {
    const x = a.split(".").map(Number);
    const y = b.split(".").map(Number);
    for (let i = 0; i < 3; i++) if ((x[i] ?? 0) !== (y[i] ?? 0)) return (x[i] ?? 0) - (y[i] ?? 0);
    return 0;
  }

  async function start() {
    error = "";
    try {
      await updateBoard();
    } catch (e) {
      error = String(e);
    }
  }

  function close() {
    clearUpdate();
    onclose();
  }
</script>

<div class="scrim" in:scrim out:scrim={{ duration: FAST }}>
  <div class="dialog" role="dialog" in:sheet out:sheet={{ duration: FAST }} aria-modal="true" aria-labelledby="upd-title">
    {#if u && (u.stage === "erasing" || u.stage === "writing" || u.stage === "restarting")}
      <Icon name="cpu" size={44} color="var(--accent)" />
      <h2 id="upd-title">Updating the board</h2>
      <p>Keep the board plugged in. It restarts on its own when done.</p>
      <div class="bar" role="progressbar" aria-label="Firmware update" aria-valuemin="0" aria-valuemax="100" aria-valuenow={u.percent}>
        <div style="width: {u.percent}%"></div>
      </div>
      <p class="status">
        <span>
          {u.stage === "erasing" ? "Erasing the board…" : u.stage === "writing" ? "Writing firmware…" : "Restarting the board…"}
        </span>
        <span class="num">{u.percent}%</span>
      </p>
    {:else if u && u.stage === "done"}
      <Icon name="check-circle" size={44} color="var(--success)" />
      <h2 id="upd-title">Board updated</h2>
      <p>The board runs fenixOS v{board || bundled}, the same as this app.</p>
      <div class="buttons"><button type="button" class="primary" onclick={close}>Done</button></div>
    {:else if u && u.stage === "failed"}
      <Icon name="warning" size={44} color="var(--warning)" />
      <h2 id="upd-title">The update did not finish</h2>
      <p>{u.error}. Keep the board plugged in and try again; the board can always be written again.</p>
      <div class="buttons">
        <button type="button" class="primary" onclick={start}>Try again</button>
        <button type="button" class="secondary" onclick={close}>Close</button>
      </div>
    {:else if mode === "reinstall"}
      <Icon name="cpu" size={44} color="var(--accent)" />
      <h2 id="upd-title">Reinstall the board firmware?</h2>
      <p>
        {#if bundled}
          This writes fenixOS v{bundled} to the board again. Your apps and settings stay.
        {:else}
          This app has no firmware to install.
        {/if}
      </p>
      <div class="buttons">
        {#if bundled}<button type="button" class="primary" onclick={start}>Reinstall</button>{/if}
        <button type="button" class="secondary" onclick={onclose}>Cancel</button>
      </div>
    {:else}
      <Icon name="warning" size={44} color="var(--warning)" />
      <h2 id="upd-title">{newer ? "Board firmware is newer than this app" : "Board firmware is out of date"}</h2>
      <p>
        The board runs fenixOS v{board}. This app is v{app}.
        {#if !bundled}
          This app has no firmware to install, so update this app instead.
        {:else if newer}
          Install v{app} on the board, or update this app, so both match. Your apps and settings stay.
        {:else}
          Update the board so both match. Your apps and settings stay.
        {/if}
      </p>
      <div class="buttons">
        {#if bundled}
          <button type="button" class="primary" onclick={start}>{newer ? `Install v${app}` : "Update board"}</button>
        {/if}
        <button type="button" class="secondary" onclick={onclose}>Later</button>
      </div>
    {/if}
    {#if error}<p class="error">{error}</p>{/if}
  </div>
</div>

<style>
  .scrim {
    position: fixed;
    inset: 40px 0 0 0;
    background: var(--scrim);
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 48px 24px;
    z-index: 20;
  }
  .dialog {
    width: 100%;
    max-width: 400px;
    background: var(--surface-2);
    border-radius: var(--radius-pc-tile);
    padding: 28px 24px 20px 24px;
    display: flex;
    flex-direction: column;
    align-items: center;
    text-align: center;
    gap: 8px;
    box-shadow: 0 24px 64px rgba(0, 0, 0, 0.6);
  }
  h2 {
    margin-top: 8px;
    font-size: 22px;
    font-weight: 700;
  }
  p {
    font-size: 15px;
    line-height: 1.5;
    color: var(--label-2);
  }
  .bar {
    width: 100%;
    margin-top: 20px;
    height: 6px;
    border-radius: 3px;
    background: var(--surface-3);
    overflow: hidden;
  }
  .bar div {
    height: 6px;
    border-radius: 3px;
    background: var(--accent);
    transition: width var(--dur-base) var(--ease);
  }
  .status {
    margin-top: 4px;
    width: 100%;
    display: flex;
    font-size: 13px;
  }
  .num {
    margin-left: auto;
    font-variant-numeric: tabular-nums;
  }
  .buttons {
    width: 100%;
    margin-top: 16px;
    display: flex;
    flex-direction: column;
    gap: 8px;
  }
  .primary,
  .secondary {
    height: 44px;
    border: 0;
    border-radius: 12px;
    font-size: 17px;
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
  .error {
    color: var(--warning);
    font-size: 13px;
  }
</style>
