<script lang="ts">
  // iOS-style switch, 51 × 31 (PcSettings, PcDashboard).
  let {
    checked,
    label,
    labelledby,
    onchange,
  }: { checked: boolean; label?: string; labelledby?: string; onchange: (v: boolean) => void } = $props();
</script>

<button
  type="button"
  role="switch"
  aria-checked={checked}
  aria-label={label}
  aria-labelledby={labelledby}
  class:on={checked}
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
    transition: background var(--dur-fast) var(--ease);
  }
  button.on {
    background: var(--success);
  }
  span {
    width: 27px;
    height: 27px;
    border-radius: 14px;
    background: #fff;
    box-shadow: 0 2px 6px rgba(0, 0, 0, 0.3);
    transition: transform var(--dur-fast) var(--ease);
  }
  button.on span {
    transform: translateX(20px);
  }
</style>
