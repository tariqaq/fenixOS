<script lang="ts">
  import { icons, type IconName } from "./icons";

  let { name, size = 18, color = "currentColor" }: { name: IconName; size?: number; color?: string } = $props();
  // The SVGs use fill="currentColor"; size them through the wrapper.
  const svg = $derived(icons[name].replace("<svg ", `<svg width="${size}" height="${size}" aria-hidden="true" `));
</script>

<span class="icon" style="width: {size}px; height: {size}px; color: {color}">{@html svg}</span>

<style>
  .icon {
    display: inline-flex;
    flex: 0 0 auto;
  }
  .icon :global(svg) {
    display: block;
  }
</style>
