<script lang="ts">
  // Window root: title bar, then the finding-the-board screen (artboard 7) until a board answers,
  // then the sidebar layout. All state comes from the engine (lib/api.ts).
  import { onDestroy } from "svelte";
  import { invoke } from "@tauri-apps/api/core";
  import TitleBar from "./lib/TitleBar.svelte";
  import Sidebar from "./lib/Sidebar.svelte";
  import LogSheet from "./lib/LogSheet.svelte";
  import UpdateDialog from "./lib/UpdateDialog.svelte";
  import Connect from "./pages/Connect.svelte";
  import Apps from "./pages/Apps.svelte";
  import Board from "./pages/Board.svelte";
  import Settings from "./pages/Settings.svelte";
  import { getState, onState, type Page, type State } from "./lib/api";

  let snap = $state<State | null>(null);
  let page = $state<Page>("apps");
  let logs = $state(false);
  // Update dialog: "Later" hides the mismatch prompt until the board connects again.
  let later = $state(false);
  let reinstall = $state(false);
  $effect(() => {
    if (snap && snap.status !== "connected") later = false;
  });
  const dialog = $derived.by(() => {
    if (!snap) return null;
    if (snap.update) return "progress" as const;
    if (reinstall) return "reinstall" as const;
    if (snap.status === "connected" && !snap.firmware_ok && !later) return "mismatch" as const;
    return null;
  });

  getState().then((s) => (snap = s));
  const stop = onState((s) => (snap = s));
  onDestroy(() => stop.then((f) => f()));

  // External links open in the default browser, never inside the app window.
  function click(e: MouseEvent) {
    const a = (e.target as HTMLElement).closest("a");
    if (a && /^https:\/\//.test(a.href)) {
      e.preventDefault();
      invoke("open_url", { url: a.href });
    }
  }
</script>

<svelte:document onclick={click} />

<div class="window">
  <TitleBar />
  {#if snap && snap.status === "connected" && snap.board}
    <div class="body">
      <Sidebar bind:page board={snap.board} />
      <main>
        {#if page === "apps"}
          <Apps apps={snap.apps} />
        {:else if page === "board"}
          <Board
            board={snap.board}
            appVersion={snap.app_version}
            onlogs={() => (logs = true)}
            onreinstall={() => (reinstall = true)}
          />
        {:else}
          <Settings appVersion={snap.app_version} boardPort={snap.board?.port ?? null} />
        {/if}
      </main>
    </div>
  {:else if snap}
    <Connect silent={snap.silent} />
  {/if}
  {#if logs}
    <LogSheet onclose={() => (logs = false)} />
  {/if}
  {#if snap && dialog}
    <UpdateDialog
      {snap}
      mode={dialog}
      onclose={() => {
        if (dialog === "mismatch") later = true;
        reinstall = false;
      }}
    />
  {/if}
</div>

<style>
  .window {
    height: 100%;
    display: flex;
    flex-direction: column;
  }
  .body {
    flex: 1;
    min-height: 0;
    display: flex;
  }
  main {
    flex: 999 1 560px;
    min-width: 0;
    padding: 32px;
    overflow: auto;
  }
</style>
