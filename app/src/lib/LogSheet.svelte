<script lang="ts">
  // Artboard 16 (PcLogs): filter, copy, open the log folder. Follows new lines while open.
  import { FAST, scrim, sheet } from "./motion";
  import { onDestroy, tick } from "svelte";
  import Icon from "./Icon.svelte";
  import { getLogs, onLog, openLogFolder, type LogLine } from "./api";

  let { onclose }: { onclose: () => void } = $props();
  type Filter = "all" | "board" | "apps" | "problems";
  const filters: { id: Filter; label: string }[] = [
    { id: "all", label: "All" },
    { id: "board", label: "Board" },
    { id: "apps", label: "Apps" },
    { id: "problems", label: "Problems" },
  ];
  let filter = $state<Filter>("all");
  let lines = $state<LogLine[]>([]);
  let list: HTMLDivElement | undefined = $state();

  const shown = $derived(
    lines.filter((l) =>
      filter === "all" ? true : filter === "problems" ? l.problem : l.source === filter,
    ),
  );
  const tag = (s: LogLine["source"]) => (s === "pc" ? "PC" : s === "board" ? "Board" : "Apps");

  async function toBottom() {
    await tick();
    if (list) list.scrollTop = list.scrollHeight;
  }

  getLogs().then((l) => {
    lines = l;
    toBottom();
  });
  const stop = onLog((l) => {
    const atEnd = !list || list.scrollTop + list.clientHeight >= list.scrollHeight - 4;
    lines = [...lines.slice(-1999), l];
    if (atEnd) toBottom();
  });
  onDestroy(() => stop.then((f) => f()));

  function copy() {
    navigator.clipboard.writeText(shown.map((l) => `${l.time} ${tag(l.source)} ${l.text}`).join("\n"));
  }
  function key(e: KeyboardEvent) {
    if (e.key === "Escape") onclose();
  }
</script>

<svelte:window onkeydown={key} />

<div class="scrim" in:scrim out:scrim={{ duration: FAST }}>
  <div class="sheet" role="dialog" in:sheet out:sheet={{ duration: FAST }} aria-labelledby="logs-title" aria-modal="true">
    <div class="head">
      <h2 id="logs-title">Logs</h2>
      <div class="seg" role="group" aria-label="Show">
        {#each filters as f (f.id)}
          <button type="button" aria-pressed={filter === f.id} class:on={filter === f.id} onclick={() => (filter = f.id)}>
            {f.label}
          </button>
        {/each}
      </div>
      <div class="actions">
        <button type="button" class="act" onclick={copy}>Copy</button>
        <button type="button" class="act" onclick={() => openLogFolder()}>Open log folder</button>
        <button type="button" class="x" aria-label="Close" onclick={onclose}><Icon name="x" size={12} color="var(--label-2)" /></button>
      </div>
    </div>
    <div class="log" role="log" bind:this={list}>
      {#each shown as l, i (i)}
        <div class="line" class:problem={l.problem}>
          <span class="t">{l.time}</span><span class="s">{tag(l.source)}</span><span class="m">{l.text}</span>
        </div>
      {/each}
    </div>
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
    padding: 40px 24px;
  }
  .sheet {
    width: 100%;
    max-width: 820px;
    height: min(640px, 100%);
    background: var(--surface-1);
    border-radius: var(--radius-pc-tile);
    display: flex;
    flex-direction: column;
    overflow: hidden;
    box-shadow: 0 24px 64px rgba(0, 0, 0, 0.6);
  }
  .head {
    padding: 18px 16px 14px 20px;
    display: flex;
    align-items: center;
    gap: 12px;
    flex-wrap: wrap;
  }
  h2 {
    font-size: 22px;
    font-weight: 700;
  }
  .seg {
    display: flex;
    gap: 2px;
    background: var(--surface-2);
    border-radius: 9px;
    padding: 2px;
    margin-left: 8px;
  }
  .seg button {
    height: 28px;
    border: 0;
    border-radius: 7px;
    background: transparent;
    color: var(--label);
    padding: 0 12px;
    font-size: 13px;
  }
  .seg button.on {
    background: var(--tile-gray);
    font-weight: 600;
  }
  .actions {
    margin-left: auto;
    display: flex;
    gap: 8px;
  }
  .act {
    height: 32px;
    border: 0;
    border-radius: var(--radius-pc-button);
    background: var(--surface-2);
    color: var(--accent);
    padding: 0 14px;
    font-size: 14px;
    font-weight: 600;
  }
  .x {
    width: 32px;
    height: 32px;
    border: 0;
    border-radius: 16px;
    background: var(--surface-2);
    display: flex;
    align-items: center;
    justify-content: center;
  }
  .log {
    flex: 1;
    overflow: auto;
    background: var(--bg);
    border-top: 1px solid var(--titlebar-line);
    padding: 8px 0;
    user-select: text;
  }
  .line {
    display: flex;
    gap: 12px;
    padding: 3px 20px;
    font-size: 13px;
    line-height: 1.5;
    font-variant-numeric: tabular-nums;
  }
  .t {
    flex: 0 0 64px;
    color: var(--label-2);
  }
  .s {
    flex: 0 0 48px;
    color: var(--label-2);
  }
  .m {
    min-width: 0;
    overflow-wrap: anywhere;
  }
  .problem .s,
  .problem .m {
    color: var(--warning);
  }
</style>
