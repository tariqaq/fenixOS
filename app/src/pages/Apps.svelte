<script lang="ts">
  // Artboard 8 (PcDashboard): app tiles with switches, "Add app", empty state. A tile opens the
  // app's settings (artboard 9); "Add app" opens the registry list (artboard 10).
  import Icon from "../lib/Icon.svelte";
  import AppIcon from "../lib/AppIcon.svelte";
  import Switch from "../lib/Switch.svelte";
  import AppSettings from "./AppSettings.svelte";
  import AddApp from "./AddApp.svelte";
  import { setAppEnabled, tileColor, type AppView } from "../lib/api";

  let { apps }: { apps: AppView[] } = $props();

  let view = $state<{ kind: "list" } | { kind: "settings"; id: string } | { kind: "add" }>({ kind: "list" });
  const open = $derived.by(() => {
    const v = view;
    return v.kind === "settings" ? apps.find((a) => a.id === v.id) : undefined;
  });

  // A removed app leaves its settings page.
  $effect(() => {
    if (view.kind === "settings" && !open) view = { kind: "list" };
  });
</script>

{#if view.kind === "settings" && open}
  <AppSettings app={open} onback={() => (view = { kind: "list" })} />
{:else if view.kind === "add"}
  <AddApp onback={() => (view = { kind: "list" })} />
{:else}
  <div class="wrap">
    <div class="head">
      <h1>Apps</h1>
      <button type="button" class="add" onclick={() => (view = { kind: "add" })}>
        <Icon name="plus" size={14} color="#FFFFFF" />
        <span>Add app</span>
      </button>
    </div>
    {#if apps.length}
      <p class="intro">Switch an app on to install it on the board. Click a tile for its settings.</p>
      <div class="grid">
        {#each apps as a (a.id)}
          <div class="tile" class:on={a.enabled} style="--c: {tileColor(a.color)}">
            <button type="button" class="open" onclick={() => (view = { kind: "settings", id: a.id })}>
              <AppIcon name={a.icon} size={28} color={a.enabled ? "#FFFFFF" : "var(--label-2)"} />
              <span class="text">
                <span class="name">{a.name}</span>
                <span class="status" class:problem={a.status === "error"}>{a.detail}</span>
              </span>
            </button>
            <div class="switch">
              <Switch tile checked={a.enabled} label="{a.name} on the board" onchange={(on) => setAppEnabled(a.id, on)} />
            </div>
          </div>
        {/each}
      </div>
    {:else}
      <div class="empty">
        <Icon name="puzzle-piece" size={40} color="var(--label-2)" />
        <p class="title">No apps yet</p>
        <p class="text-empty">Add an app from the fenixOS registry. It shows up here, and on the board once you switch it on.</p>
        <button type="button" class="link" onclick={() => (view = { kind: "add" })}>Browse apps</button>
      </div>
    {/if}
  </div>
{/if}

<style>
  .wrap {
    max-width: 1120px;
    margin: 0 auto;
    display: flex;
    flex-direction: column;
    gap: 24px;
  }
  .head {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
  }
  .add {
    height: 36px;
    border: 0;
    border-radius: var(--radius-pc-button);
    background: var(--accent-fill);
    color: #fff;
    padding: 0 14px;
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 15px;
    font-weight: 600;
  }
  .intro {
    font-size: 15px;
    color: var(--label-2);
  }
  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
    gap: 16px;
  }
  .tile {
    height: 140px;
    border-radius: var(--radius-pc-tile);
    background: var(--surface-2);
    position: relative;
    transition: background var(--dur-fast) var(--ease);
  }
  .tile.on {
    background: var(--c);
  }
  .open {
    position: absolute;
    inset: 0;
    border: 0;
    border-radius: var(--radius-pc-tile);
    background: transparent;
    padding: 18px;
    color: #fff;
    display: flex;
    flex-direction: column;
    justify-content: space-between;
    align-items: flex-start;
    text-align: left;
  }
  .text {
    display: flex;
    flex-direction: column;
    gap: 2px;
    padding-right: 64px;
  }
  .name {
    font-size: 17px;
    font-weight: 600;
  }
  .status {
    font-size: 12px;
    font-weight: 500;
    color: var(--label-3);
  }
  .tile.on .status {
    color: #fff;
  }
  .status.problem {
    color: var(--warning);
  }
  .switch {
    position: absolute;
    right: 18px;
    bottom: 18px;
  }
  .empty {
    background: var(--surface-1);
    border-radius: var(--radius-pc-tile);
    padding: 48px 24px;
    display: flex;
    flex-direction: column;
    align-items: center;
    text-align: center;
    gap: 10px;
  }
  .title {
    margin-top: 6px;
    font-size: 22px;
    font-weight: 600;
  }
  .text-empty {
    max-width: 420px;
    font-size: 15px;
    line-height: 1.5;
    color: var(--label-2);
  }
  .link {
    margin-top: 8px;
    border: 0;
    background: transparent;
    padding: 0;
    color: var(--accent);
    font-size: 15px;
    font-weight: 600;
  }
</style>
