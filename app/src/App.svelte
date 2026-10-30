<script lang="ts">
  // Window root: title bar, then the finding-the-board screen (artboard 7) until a board answers,
  // then the sidebar layout. The board screen stays over the layout for its plug-in animation and
  // fades out (P11). A board already connected at start opens the layout at once. All state
  // comes from the engine (lib/api.ts).
  import { onDestroy } from "svelte";
  import { invoke } from "@tauri-apps/api/core";
  import TitleBar from "./lib/TitleBar.svelte";
  import Sidebar from "./lib/Sidebar.svelte";
  import LogSheet from "./lib/LogSheet.svelte";
  import UpdateDialog from "./lib/UpdateDialog.svelte";
  import Connect from "./pages/Connect.svelte";
  import Apps from "./pages/Apps.svelte";
  import Board from "./pages/Board.svelte";
  import TaskManager from "./pages/TaskManager.svelte";
  import Settings from "./pages/Settings.svelte";
  import { getState, onState, type Page, type State } from "./lib/api";
  import { enter } from "./lib/motion";

  let snap = $state<State | null>(null);
  let page = $state<Page>("apps");
  let logs = $state(false);
  // Update dialog: "Later" hides the mismatch prompt until the board connects again.
  let later = $state(false);
  let reinstall = $state(false);
  // The board screen: up while no board answers, then through its plug-in run and fade.
  let looking = $state(false);
  let leaving = $state(false);
  const connected = $derived(!!snap && snap.status === "connected" && !!snap.board);
  const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  let fadeTimer = 0;
  $effect(() => {
    if (snap && snap.status !== "connected") {
      later = false;
      looking = true;
      leaving = false;
      clearTimeout(fadeTimer);
    }
  });
  function connectDone() {
    leaving = true;
    fadeTimer = window.setTimeout(() => (looking = leaving = false), reduced ? 250 : 600);
  }
  const dialog = $derived.by(() => {
    if (!snap) return null;
    if (snap.update) return "progress" as const;
    if (reinstall) return "reinstall" as const;
    if (snap.status === "connected" && !snap.firmware_ok && !later && !looking) return "mismatch" as const;
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
  {#if snap && connected && snap.board}
    <div class="body">
      <Sidebar bind:page board={snap.board} />
      <main>
        <!-- A new page fades in rising 8 px; the old one leaves at once, so the two never stack. -->
        {#key page}
          <div in:enter>
            {#if page === "apps"}
              <Apps apps={snap.apps} />
            {:else if page === "board"}
              <Board
                board={snap.board}
                appVersion={snap.app_version}
                streaming={snap.streaming}
                onlogs={() => (logs = true)}
                onreinstall={() => (reinstall = true)}
              />
            {:else if page === "tasks"}
              <TaskManager board={snap.board} onreinstall={() => (reinstall = true)} />
            {:else}
              <Settings appVersion={snap.app_version} boardPort={snap.board?.port ?? null} radio={snap.board?.radio ?? null} />
            {/if}
          </div>
        {/key}
      </main>
    </div>
  {/if}
  {#if snap && looking}
    <div class="over" class:leaving>
      <Connect silent={snap.silent} {connected} appVersion={snap.app_version} ondone={connectDone} />
    </div>
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
    position: relative;
    height: 100%;
    display: flex;
    flex-direction: column;
  }
  /* The board screen covers everything under the title bar (40 px). */
  .over {
    position: absolute;
    inset: 40px 0 0 0;
    display: flex;
    transition: opacity 600ms cubic-bezier(0.2, 0.8, 0.2, 1);
  }
  .over.leaving {
    opacity: 0;
    pointer-events: none;
  }
  @media (prefers-reduced-motion: reduce) {
    .over {
      transition-duration: 250ms;
    }
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
