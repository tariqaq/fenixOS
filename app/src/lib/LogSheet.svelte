<script lang="ts">
  // Artboard 16 (PcLogs): filter, copy, open the log folder. Follows new lines while open.
  // "On the card" (P53) reads the board's saved logs and crash records from its SD card.
  import { FAST, scrim, sheet } from "./motion";
  import { onDestroy, tick } from "svelte";
  import Icon from "./Icon.svelte";
  import { boardCardFile, boardCardLogs, cardLine, getLogs, onLog, openLogFolder, type CardLog, type LogLine } from "./api";

  let { onclose }: { onclose: () => void } = $props();
  type Filter = "all" | "board" | "apps" | "problems" | "card";
  const filters: { id: Filter; label: string }[] = [
    { id: "all", label: "All" },
    { id: "board", label: "Board" },
    { id: "apps", label: "Apps" },
    { id: "problems", label: "Problems" },
    { id: "card", label: "On the card" },
  ];
  let filter = $state<Filter>("all");
  let lines = $state<LogLine[]>([]);
  let list: HTMLDivElement | undefined = $state();

  // On the card: the saved files, the one shown and its lines.
  let files = $state<CardLog[]>([]);
  let picked = $state<string>("");
  let cardLines = $state<LogLine[]>([]);
  let cardNote = $state("");

  async function openCard() {
    cardNote = "Reading the board's SD card…";
    cardLines = [];
    try {
      files = await boardCardLogs();
      const first = files.find((f) => !f.crash) ?? files[0];
      if (!first) {
        cardNote = "No saved logs on the card yet.";
        return;
      }
      await pick(first);
    } catch (e) {
      files = [];
      cardNote = `Could not read the card: ${e}`;
    }
  }
  async function pick(f: CardLog) {
    picked = f.path;
    cardNote = "Reading…";
    try {
      const text = await boardCardFile(f.path);
      cardLines = text
        .split("\n")
        .filter((l) => l.length)
        .map((l) => cardLine(l, f.crash));
      cardNote = "";
      toBottom();
    } catch (e) {
      cardLines = [];
      cardNote = `Could not read ${f.path}: ${e}`;
    }
  }
  function choose(id: Filter) {
    filter = id;
    if (id === "card") openCard();
    else toBottom();
  }

  const shown = $derived(
    filter === "card"
      ? cardLines
      : lines.filter((l) => (filter === "all" ? true : filter === "problems" ? l.problem : l.source === filter)),
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
    if (atEnd && filter !== "card") toBottom();
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
          <button type="button" aria-pressed={filter === f.id} class:on={filter === f.id} onclick={() => choose(f.id)}>
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
    {#if filter === "card" && files.length}
      <div class="files" role="group" aria-label="Saved on the card">
        {#each files as f (f.path)}
          <button type="button" class="file" class:on={picked === f.path} class:crash={f.crash} aria-pressed={picked === f.path} onclick={() => pick(f)}>
            {f.crash ? `Crash ${f.boot}` : `Boot ${f.boot}`}
          </button>
        {/each}
      </div>
    {/if}
    <div class="log" role="log" bind:this={list}>
      {#if filter === "card" && cardNote}
        <p class="note">{cardNote}</p>
      {/if}
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
    box-shadow: var(--shadow-sheet);
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
    color: var(--label);
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
  .files {
    display: flex;
    gap: 6px;
    padding: 0 20px 10px;
    overflow-x: auto;
    flex: 0 0 auto;
  }
  .file {
    height: 26px;
    border: 0;
    border-radius: 13px;
    background: var(--surface-2);
    color: var(--label);
    padding: 0 12px;
    font-size: 13px;
    white-space: nowrap;
  }
  .file.on {
    background: var(--tile-gray);
    font-weight: 600;
  }
  .file.crash {
    color: var(--warning);
  }
  .note {
    padding: 8px 20px;
    font-size: 13px;
    color: var(--label-2);
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
