<script lang="ts">
  // iOS-style switch, 51 × 31 (PcSettings, PcAppSettings). `tile`: the PcDashboard variant on a
  // coloured tile (--switch-tile-on / --switch-tile-off tracks, app.css).
  let {
    checked,
    label,
    labelledby,
    tile = false,
    onchange,
  }: {
    checked: boolean;
    label?: string;
    labelledby?: string;
    tile?: boolean;
    onchange: (v: boolean) => void;
  } = $props();
</script>

<button
  type="button"
  role="switch"
  aria-checked={checked}
  aria-label={label}
  aria-labelledby={labelledby}
  class:on={checked}
  class:tile
  onclick={() => onchange(!checked)}
>
  <span aria-hidden="true"></span>
</button>

<style>
  button {
    width: 51px;
    height: 31px;
    flex: 0 0 51px;
    border: 0;
    border-radius: 16px;
    background: var(--surface-3);
    padding: 2px;
    display: flex;
    align-items: center;
    transition:
      background var(--dur-fast) var(--ease),
      transform var(--dur-fast) var(--ease);
  }
  button.on {
    background: var(--success);
  }
  button.tile {
    background: var(--switch-tile-off);
  }
  button.tile.on {
    background: var(--switch-tile-on);
  }
  span {
    width: 27px;
    height: 27px;
    border-radius: 14px;
    background: var(--label);
    box-shadow: var(--shadow-knob);
    transition: transform var(--dur-fast) var(--ease);
  }
  button.on span {
    transform: translateX(20px);
  }
</style>
