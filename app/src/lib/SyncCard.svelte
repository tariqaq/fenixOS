<script lang="ts">
  // Apps › Sync with board (P51): what differs between this PC and the board after it connects,
  // one choice per difference, applied together on one click. Board-only apps get their PC part
  // from the registry when it has them; otherwise they are kept on the board alone or removed.
  import { listRegistry, syncApps, type SyncAction, type SyncItem } from "./api";

  let { items }: { items: SyncItem[] } = $props();

  // Ids the registry has (board-only rows offer "Add on this PC" only for those); null = not read.
  let inRegistry = $state<Set<string> | null>(null);
  let picked = $state<Record<string, SyncAction | "skip">>({});
  let busy = $state(false);
  let error = $state("");

  $effect(() => {
    if (inRegistry || !items.some((i) => i.kind === "board_only")) return;
    listRegistry()
      .then((r) => (inRegistry = new Set(r.map((a) => a.id))))
      .catch(() => (inRegistry = new Set()));
  });

  function options(i: SyncItem): { value: SyncAction | "skip"; label: string }[] {
    switch (i.kind) {
      case "board_missing":
        return [{ value: "board", label: "Put on the board" }];
      case "board_older":
        return [{ value: "board", label: `Update the board to v${i.pc_version}` }];
      case "board_differs":
        return [{ value: "board", label: "Put this PC's copy on the board" }];
      case "pc_older":
        return [{ value: "pc", label: `Update this PC to v${i.board_version}` }];
      default: {
        const o: { value: SyncAction | "skip"; label: string }[] = [];
        if (inRegistry?.has(i.id)) o.push({ value: "pc", label: "Add it on this PC" });
        o.push({ value: "keep", label: "Keep it on the board only" });
        if (!i.card_missing) o.push({ value: "remove", label: "Remove it from the board" });
        return o;
      }
    }
  }

  const choice = (i: SyncItem) => picked[i.id] ?? options(i)[0].value;

  function text(i: SyncItem): string {
    switch (i.kind) {
      case "board_missing":
        return `v${i.pc_version} is on this PC, not on the board`;
      case "board_older":
        return `Board has v${i.board_version}, this PC v${i.pc_version}`;
      case "board_differs":
        return `v${i.pc_version} on both, but the copies differ`;
      case "pc_older":
        return `This PC has v${i.pc_version}, the board v${i.board_version}`;
      default:
        return i.card_missing
          ? `v${i.board_version} on the board's SD card, which is out`
          : `v${i.board_version} on the board only, not on this PC`;
    }
  }

  async function sync() {
    busy = true;
    error = "";
    const choices = items
      .map((i) => ({ id: i.id, action: choice(i) }))
      .filter((c): c is { id: string; action: SyncAction } => c.action !== "skip");
    try {
      await syncApps(choices);
    } catch (e) {
      error = String(e);
    }
    busy = false;
  }
</script>

<section class="card">
  <div class="top">
    <div>
      <h2>Sync with board</h2>
      <p class="sub">
        This PC and the board differ in {items.length === 1 ? "1 app" : `${items.length} apps`}. Pick what to do, then
        sync.
      </p>
    </div>
    <button type="button" class="sync" disabled={busy} onclick={sync}>{busy ? "Syncing" : "Sync"}</button>
  </div>
  <div class="group">
    {#each items as i (i.id)}
      <div class="row">
        <span class="text">
          <span class="name">{i.name}</span>
          <span class="what">{text(i)}</span>
        </span>
        <select
          class="end"
          aria-label="What to do with {i.name}"
          value={choice(i)}
          onchange={(e) => (picked[i.id] = (e.target as HTMLSelectElement).value as SyncAction | "skip")}
        >
          {#each options(i) as o (o.value)}
            <option value={o.value}>{o.label}</option>
          {/each}
          <option value="skip">Leave it for now</option>
        </select>
      </div>
    {/each}
  </div>
  {#if error}
    <p class="error">{error}</p>
  {/if}
</section>

<style>
  .card {
    display: flex;
    flex-direction: column;
    gap: 12px;
  }
  .top {
    display: flex;
    align-items: flex-end;
    justify-content: space-between;
    gap: 12px;
  }
  h2 {
    font-size: 20px;
    font-weight: 600;
  }
  .sub {
    margin-top: 4px;
    font-size: 15px;
    color: var(--label-2);
  }
  .sync {
    height: 36px;
    border: 0;
    border-radius: var(--radius-pc-button);
    background: var(--accent-fill);
    color: var(--label);
    padding: 0 18px;
    font-size: 15px;
    font-weight: 600;
    flex: none;
  }
  .sync:disabled {
    opacity: 0.6;
  }
  .text {
    display: flex;
    flex-direction: column;
    gap: 2px;
    min-width: 0;
  }
  .name {
    font-weight: 600;
  }
  .what {
    font-size: 13px;
    color: var(--label-2);
  }
  .end {
    margin-left: auto;
  }
  select {
    height: 30px;
    border: 0;
    border-radius: 8px;
    background: var(--surface-2);
    color: var(--label);
    padding: 0 10px;
    font-family: inherit;
    font-size: 14px;
  }
  select:focus-visible {
    outline: 2px solid var(--accent);
    outline-offset: 2px;
  }
  .error {
    font-size: 13px;
    color: var(--warning);
  }
</style>
