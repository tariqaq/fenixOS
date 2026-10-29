<script lang="ts">
  // Artboard 7 (PcConnect): looking for the board, with CH340 driver help.
  import Icon from "../lib/Icon.svelte";
  import type { Silent } from "../lib/api";

  let { silent }: { silent: Silent[] } = $props();
  const lines = [1, 0.85, 0.7, 0.55, 0.45, 0.35, 0.28, 0.2];
</script>

<div class="page">
  <div class="col">
    <div class="disc"><Icon name="usb" size={44} color="var(--accent)" /></div>
    <h1>Looking for your board</h1>
    <p class="lead">Connect the ESP32 board to this PC with a USB cable. fenixOS finds it on its own and connects.</p>
    <div class="checking">
      <svg class="spin" role="img" aria-label="Searching" width="18" height="18" viewBox="0 0 22 22">
        <g stroke="#FFFFFF" stroke-width="2" stroke-linecap="round">
          {#each lines as o, i}
            <line x1="11" y1="2.5" x2="11" y2="6.5" opacity={o} transform="rotate({i * 45} 11 11)" />
          {/each}
        </g>
      </svg>
      <span>Checking USB serial ports</span>
    </div>
    {#if silent.length}
      <div class="card">
        <p class="card-title">A board on {silent[0].port} did not answer</p>
        <p class="card-text">
          It may have no fenixOS on it yet, or another firmware. Setting up a new board comes in a later version of
          this app.
        </p>
      </div>
    {/if}
    <div class="card">
      <p class="card-title">Board not showing up?</p>
      <p class="card-text">
        Windows needs the CH340 USB driver for this board. Use a USB-A to USB-C or micro-USB cable that carries data.
      </p>
      <a href="https://www.wch-ic.com/downloads/CH341SER_EXE.html" target="_blank" rel="noreferrer">Get the CH340 driver from WCH</a>
    </div>
  </div>
</div>

<style>
  .page {
    flex: 1;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 48px 24px;
    overflow: auto;
  }
  .col {
    max-width: 480px;
    display: flex;
    flex-direction: column;
    align-items: center;
    text-align: center;
    gap: 12px;
  }
  .disc {
    width: 88px;
    height: 88px;
    border-radius: 44px;
    background: var(--surface-1);
    display: flex;
    align-items: center;
    justify-content: center;
  }
  h1 {
    margin-top: 12px;
    line-height: 1.15;
  }
  .lead {
    font-size: 15px;
    line-height: 1.5;
    color: var(--label-2);
  }
  .checking {
    margin-top: 12px;
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 14px;
    color: var(--label-2);
  }
  .spin {
    animation: spin 0.8s steps(8) infinite;
  }
  @keyframes spin {
    to {
      transform: rotate(360deg);
    }
  }
  .card {
    margin-top: 28px;
    width: 100%;
    background: var(--surface-1);
    border-radius: var(--radius-pc-card);
    padding: 16px 18px;
    text-align: left;
    display: flex;
    flex-direction: column;
    gap: 6px;
  }
  .card + .card {
    margin-top: 0;
  }
  .card-title {
    font-size: 15px;
    font-weight: 600;
  }
  .card-text {
    font-size: 14px;
    line-height: 1.5;
    color: var(--label-2);
  }
  .card a {
    font-size: 14px;
  }
</style>
