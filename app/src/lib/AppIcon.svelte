<script lang="ts">
  // An app's own icon: any Phosphor icon (fill weight) named in its manifest. Each icon is a
  // separate lazy chunk, so only the icons of added or listed apps are ever loaded.
  const all = import.meta.glob("/node_modules/@phosphor-icons/core/assets/fill/*.svg", {
    query: "?raw",
    import: "default",
  }) as Record<string, () => Promise<string>>;
  const path = (n: string) => `/node_modules/@phosphor-icons/core/assets/fill/${n}-fill.svg`;

  let { name, size = 22, color = "var(--label)" }: { name: string; size?: number; color?: string } = $props();

  let svg = $state("");
  $effect(() => {
    const load = all[path(name)] ?? all[path("puzzle-piece")];
    let live = true;
    load().then((s) => {
      if (live) svg = s.replace("<svg ", `<svg width="${size}" height="${size}" aria-hidden="true" `);
    });
    return () => (live = false);
  });
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
