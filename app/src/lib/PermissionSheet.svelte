<script lang="ts">
  // Artboard 15 (PcPermissions): what an app's PC part will do, shown before Add.
  import { FAST, scrim, sheet } from "./motion";
  import AppIcon from "./AppIcon.svelte";
  import { permissionRows, tileColor, type RegistryApp } from "./api";

  let { app, onadd, oncancel }: { app: RegistryApp; onadd: () => void; oncancel: () => void } = $props();

  const rows = $derived(permissionRows(app.permissions, !!app.pc, !!app.pc?.elevated));
  const source = $derived(app.repo.replace(/^https:\/\/github\.com\//, ""));
</script>

<svelte:window onkeydown={(e) => e.key === "Escape" && oncancel()} />

<div class="scrim" in:scrim out:scrim={{ duration: FAST }}>
  <div class="sheet" role="dialog" in:sheet out:sheet={{ duration: FAST }} aria-modal="true" aria-labelledby="perm-title">
    <div class="head">
      <div class="badge" style="background: {tileColor(app.color)}"><AppIcon name={app.icon} size={26} /></div>
      <div class="title">
        <h2 id="perm-title">Add {app.name}?</h2>
        <p>v{app.version} from {source}</p>
      </div>
    </div>
    {#if rows.length}
      <p class="lead">
        {app.pc ? "Its PC part runs on this computer while the app is switched on. It says it will:" : "It runs on the board only. It says it will:"}
      </p>
      <div class="group">
        {#each rows as r (r.key)}
          <div class="row">
            <AppIcon name={r.icon} size={20} color="var(--accent)" />
            <p>{r.text}</p>
          </div>
        {/each}
      </div>
    {:else}
      <p class="lead">It runs on the board only and has no PC part.</p>
    {/if}
    <p class="fine">
      fenixOS only lists apps from the registry, checks each download against its checksum, and starts the PC part only
      while you have it switched on.
    </p>
    <div class="buttons">
      <button type="button" class="primary" onclick={onadd}>Add app</button>
      <button type="button" class="secondary" onclick={oncancel}>Cancel</button>
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
    padding: 48px 24px;
    z-index: 10;
  }
  .sheet {
    width: 100%;
    max-width: 440px;
    background: var(--surface-2);
    border-radius: var(--radius-pc-tile);
    padding: 28px 24px 20px 24px;
    display: flex;
    flex-direction: column;
    gap: 16px;
    box-shadow: 0 24px 64px rgba(0, 0, 0, 0.6);
  }
  .head {
    display: flex;
    align-items: center;
    gap: 14px;
  }
  .badge {
    width: 52px;
    height: 52px;
    flex: 0 0 52px;
    border-radius: 13px;
    display: flex;
    align-items: center;
    justify-content: center;
  }
  .title {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }
  h2 {
    font-size: 22px;
    font-weight: 700;
  }
  .title p {
    font-size: 14px;
    color: var(--label-2);
  }
  .lead {
    font-size: 15px;
    line-height: 1.5;
    color: var(--label-2);
  }
  .row {
    padding: 10px 14px;
  }
  .fine {
    font-size: 13px;
    line-height: 1.5;
    color: var(--label-2);
  }
  .buttons {
    display: flex;
    flex-direction: column;
    gap: 8px;
    margin-top: 4px;
  }
  .primary,
  .secondary {
    height: 44px;
    border: 0;
    border-radius: 12px;
    font-size: 17px;
  }
  .primary {
    background: var(--accent-fill);
    color: #fff;
    font-weight: 600;
  }
  .secondary {
    background: transparent;
    color: var(--accent);
  }
</style>
