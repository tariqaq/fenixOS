<script lang="ts">
  // Artboard 10 (PcAddApp): registry list with search and Add buttons. Add shows the permissions
  // sheet (artboard 15) first; the engine then downloads, checks and switches the app on.
  import Icon from "../lib/Icon.svelte";
  import AppIcon from "../lib/AppIcon.svelte";
  import PermissionSheet from "../lib/PermissionSheet.svelte";
  import { addApp, listRegistry, tileColor, type RegistryApp } from "../lib/api";
  import { enter } from "../lib/motion";

  let { onback }: { onback: () => void } = $props();

  let apps = $state<RegistryApp[] | null>(null);
  let error = $state("");
  let query = $state("");
  let asking = $state<RegistryApp | null>(null);
  let busy = $state<Record<string, string>>({});

  function load() {
    error = "";
    apps = null;
    listRegistry()
      .then((a) => (apps = a))
      .catch((e) => (error = String(e)));
  }
  load();

  const shown = $derived(
    (apps ?? []).filter((a) =>
      `${a.name} ${a.description}`.toLowerCase().includes(query.trim().toLowerCase()),
    ),
  );

  async function add(a: RegistryApp) {
    asking = null;
    error = "";
    busy[a.id] = "Adding";
    try {
      await addApp(a.id);
      a.added = true;
      delete busy[a.id];
    } catch (e) {
      // The error shows below the list; Add comes back so it can be tried again (P42).
      delete busy[a.id];
      error = `${a.name}: ${e}`;
    }
  }
</script>

<div class="wrap">
  <button type="button" class="back" onclick={onback}>
    <Icon name="caret-left" size={14} color="var(--accent)" />
    <span>Apps</span>
  </button>
  <h1>Add app</h1>
  <div class="search">
    <label for="appsearch" class="hidden">Search apps</label>
    <Icon name="magnifying-glass" size={16} color="var(--label-2)" />
    <input id="appsearch" type="search" placeholder="Search apps" bind:value={query} />
  </div>

  {#if apps === null && !error}
    <p class="msg">Reading the registry…</p>
  {:else if error && apps === null}
    <div class="msg">
      <p>Could not read the registry: {error}</p>
      <button type="button" class="link" onclick={load}>Try again</button>
    </div>
  {:else if shown.length === 0}
    <p class="msg">{query ? "No apps match your search." : "The registry has no apps yet."}</p>
  {:else}
    <div class="group" in:enter={{ y: 6 }}>
      {#each shown as a (a.id)}
        <div class="item">
          <div class="badge" style="background: {tileColor(a.color)}"><AppIcon name={a.icon} size={22} /></div>
          <div class="text">
            <p class="name">{a.name}</p>
            <p class="desc">{a.description}</p>
          </div>
          {#if a.added}
            <span class="done"><Icon name="check" size={14} color="var(--label-2)" /><span>Added</span></span>
          {:else if busy[a.id]}
            <span class="done">Adding…</span>
          {:else}
            <button type="button" class="get" aria-label="Add {a.name}" onclick={() => (asking = a)}>
              <Icon name="download-simple" size={14} color="var(--accent)" />
              <span>Add</span>
            </button>
          {/if}
        </div>
      {/each}
    </div>
  {/if}
  {#if error && apps !== null}
    <p class="msg problem">{error}</p>
  {/if}
  <p class="foot">
    Apps are listed from the fenixOS registry on GitHub (<a href="https://github.com/tariqaq/fenixOS">tariqaq/fenixOS</a>,
    the registry folder). Each app downloads from its own repo, and fenixOS checks it against the checksum in its manifest
    before installing.
  </p>
</div>

{#if asking}
  <PermissionSheet app={asking} onadd={() => asking && add(asking)} oncancel={() => (asking = null)} />
{/if}

<style>
  .wrap {
    max-width: 760px;
    margin: -8px auto 0 auto;
    display: flex;
    flex-direction: column;
    gap: 20px;
  }
  .back,
  .link {
    align-self: flex-start;
    border: 0;
    background: transparent;
    padding: 0;
    color: var(--accent);
    display: flex;
    align-items: center;
    gap: 4px;
    font-size: 15px;
  }
  .search {
    position: relative;
    display: flex;
  }
  .search :global(.icon) {
    position: absolute;
    left: 12px;
    top: 12px;
  }
  .search input {
    width: 100%;
    height: 40px;
    border: 0;
    border-radius: var(--radius-pc-button);
    background: var(--surface-1);
    color: var(--label);
    padding: 0 12px 0 36px;
    font-family: inherit;
    font-size: 15px;
    user-select: text;
  }
  .search input::placeholder {
    color: var(--label-2);
  }
  .search input:focus-visible {
    outline: 2px solid var(--accent);
    outline-offset: 2px;
  }
  .hidden {
    position: absolute;
    width: 1px;
    height: 1px;
    overflow: hidden;
    clip: rect(0 0 0 0);
  }
  .item {
    min-height: 72px;
    padding: 12px 16px;
    display: flex;
    align-items: center;
    gap: 14px;
    border-bottom: 1px solid var(--separator);
  }
  .item:last-child {
    border-bottom: 0;
  }
  .badge {
    width: 44px;
    height: 44px;
    flex: 0 0 44px;
    border-radius: 11px;
    display: flex;
    align-items: center;
    justify-content: center;
  }
  .text {
    flex: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 2px;
  }
  .name {
    font-size: 15px;
    font-weight: 600;
  }
  .desc {
    font-size: 14px;
    color: var(--label-2);
  }
  .get {
    height: 32px;
    border: 0;
    border-radius: 16px;
    background: var(--surface-2);
    color: var(--label);
    padding: 0 16px;
    font-size: 15px;
    font-weight: 600;
    display: flex;
    align-items: center;
    gap: 6px;
  }
  .done {
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 15px;
    color: var(--label-2);
  }
  .msg {
    font-size: 15px;
    color: var(--label-2);
    display: flex;
    flex-direction: column;
    gap: 8px;
  }
  .problem {
    color: var(--warning);
  }
  .foot {
    margin-left: 4px;
    font-size: 13px;
    line-height: 1.5;
    color: var(--label-2);
  }
</style>
