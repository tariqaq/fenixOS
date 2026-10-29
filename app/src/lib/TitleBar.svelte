<script lang="ts">
  // Custom dark title bar (all PC artboards): name on the left, Windows-style buttons on the right.
  // Close destroys the window; fenixOS stays in the tray.
  import { getCurrentWindow } from "@tauri-apps/api/window";

  const win = getCurrentWindow();
</script>

<header class="bar" data-tauri-drag-region>
  <p data-tauri-drag-region>fenixOS</p>
  <div class="buttons">
    <button type="button" aria-label="Minimize" onclick={() => win.minimize()}>
      <svg width="10" height="10" viewBox="0 0 10 10" aria-hidden="true"><line x1="0" y1="5" x2="10" y2="5" stroke="#98989F" stroke-width="1" /></svg>
    </button>
    <button type="button" aria-label="Maximize" onclick={() => win.toggleMaximize()}>
      <svg width="10" height="10" viewBox="0 0 10 10" aria-hidden="true"><rect x="0.5" y="0.5" width="9" height="9" fill="none" stroke="#98989F" stroke-width="1" /></svg>
    </button>
    <button type="button" class="close" aria-label="Close to tray" onclick={() => win.close()}>
      <svg width="10" height="10" viewBox="0 0 10 10" aria-hidden="true"><path d="M0 0 L10 10 M10 0 L0 10" stroke="#98989F" stroke-width="1" /></svg>
    </button>
  </div>
</header>

<style>
  .bar {
    height: 40px;
    flex: 0 0 40px;
    display: flex;
    align-items: center;
    background: var(--titlebar);
    border-bottom: 1px solid var(--titlebar-line);
  }
  p {
    margin: 0 0 0 16px;
    font-size: 13px;
    font-weight: 600;
    color: var(--label-2);
  }
  .buttons {
    margin-left: auto;
    display: flex;
  }
  button {
    width: 46px;
    height: 40px;
    border: 0;
    background: transparent;
    display: flex;
    align-items: center;
    justify-content: center;
  }
  button:hover {
    background: var(--surface-2);
    filter: none;
  }
  button:active {
    transform: none;
  }
  .close:hover {
    background: var(--danger-fill);
  }
  .close:hover path {
    stroke: #fff;
  }
</style>
