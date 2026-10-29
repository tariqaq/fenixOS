<script lang="ts">
  // First-run experience (PLAN §3.9): welcome, board check (3 s wait if already plugged, else
  // the plug-in loop), firmware check (fenixOS: sync; anything else: probe, confirm, erase and
  // install), done, then the main window. Runs in its own GPU-on window (docs/decisions.md, S8).
  import { onDestroy, onMount } from "svelte";
  import { invoke } from "@tauri-apps/api/core";
  import { getCurrentWindow } from "@tauri-apps/api/window";
  import {
    finishWelcome,
    getState,
    installBoard,
    onState,
    probeBoard,
    updateBoard,
    type Probe,
    type State,
  } from "../lib/api";
  import { BoardScene } from "./scene";

  type Step = "welcome" | "looking" | "plug" | "checking" | "confirm" | "outdated" | "installing" | "failed" | "done";

  const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  let canvas: HTMLCanvasElement;
  let scene: BoardScene | undefined;
  let snap = $state<State | null>(null);
  let step = $state<Step>("welcome");
  let probe = $state<Probe | null>(null);
  let error = $state("");
  const showFps = new URLSearchParams(location.search).has("fps");
  let fps = $state(0);
  const timers: number[] = [];
  const later = (ms: number, f: () => void) => timers.push(window.setTimeout(f, reduced ? Math.min(ms, 600) : ms));

  const present = (s: State | null) => !!s && (s.status === "connected" || s.silent.length > 0);
  const u = $derived(snap?.update ?? null);
  const percent = $derived(u ? u.percent : 0);

  onMount(() => {
    scene = new BoardScene(canvas, reduced);
    const resize = () => scene?.resize();
    window.addEventListener("resize", resize);
    getState().then((s) => {
      snap = s;
      if (scene) scene.version = s.app_version;
    });
    // Let the wordmark and the board turn in, then look for the board.
    requestAnimationFrame(() => scene?.setPose("intro"));
    later(2600, boardCheck);
    if (showFps) timers.push(window.setInterval(() => (fps = scene?.fps ?? 0), 1000));
    return () => window.removeEventListener("resize", resize);
  });
  const stop = onState((s) => {
    snap = s;
    onChange();
  });
  onDestroy(() => {
    stop.then((f) => f());
    timers.forEach((t) => (clearTimeout(t), clearInterval(t)));
    scene?.dispose();
  });

  function boardCheck() {
    scene?.setPose("waiting");
    if (present(snap)) {
      step = "looking";
      later(3000, plugged);
    } else {
      step = "plug";
    }
  }

  // Engine state changed: react where the flow is waiting on it.
  function onChange() {
    if (!snap) return;
    if (step === "plug" && present(snap)) plugged();
    if (step === "installing" && u?.stage === "done") finish();
    if (step === "installing" && u?.stage === "failed") {
      error = u.error;
      step = "failed";
    }
    if (scene && step === "installing") {
      scene.mode = "install";
      scene.progress = u?.stage === "restarting" ? 1 : percent / 100;
    }
  }

  function plugged() {
    scene?.setPose("plugged");
    step = "checking";
    later(900, firmwareCheck);
  }

  async function firmwareCheck() {
    const s = snap;
    if (!s || !present(s)) {
      scene?.setPose("waiting");
      step = "plug";
      return;
    }
    if (s.status === "connected") {
      if (s.firmware_ok) {
        if (scene) scene.mode = "boot";
        later(1500, finish);
      } else {
        step = "outdated";
      }
      return;
    }
    // A CH340 that does not answer as fenixOS: ask its bootloader what it is.
    try {
      probe = await probeBoard();
      step = "confirm";
    } catch (e) {
      error = String(e);
      step = "failed";
    }
  }

  async function install(erase: boolean) {
    error = "";
    step = "installing";
    scene?.setPose("front");
    if (scene) {
      scene.mode = "install";
      scene.progress = 0;
    }
    try {
      await (erase ? installBoard() : updateBoard());
    } catch (e) {
      error = String(e);
      step = "failed";
    }
  }

  function finish() {
    scene?.setPose("front");
    if (scene) scene.mode = "home";
    step = "done";
    later(3500, () => finishWelcome(true));
  }

  const skip = () => finishWelcome(true);
  const close = () => finishWelcome(false);
  const win = getCurrentWindow();

  // Links open in the browser, not in this window.
  function click(e: MouseEvent) {
    const a = (e.target as HTMLElement).closest("a");
    if (a?.href.startsWith("https://")) {
      e.preventDefault();
      invoke("open_url", { url: a.href });
    }
  }
</script>

<svelte:window onclick={click} />

<canvas bind:this={canvas}></canvas>

{#if showFps}<p class="fps">{fps.toFixed(0)} fps</p>{/if}

<header data-tauri-drag-region>
  <button type="button" class="skip" onclick={skip}>Skip</button>
  <button type="button" class="ctl" aria-label="Minimize" onclick={() => win.minimize()}>
    <svg width="10" height="10" viewBox="0 0 10 10" aria-hidden="true"><line x1="0" y1="5" x2="10" y2="5" stroke="#98989F" /></svg>
  </button>
  <button type="button" class="ctl" aria-label="Close" onclick={close}>
    <svg width="10" height="10" viewBox="0 0 10 10" aria-hidden="true"><path d="M0 0 L10 10 M10 0 L0 10" stroke="#98989F" /></svg>
  </button>
</header>

{#key step}
  <section class="copy" class:center={step === "welcome"} aria-live="polite">
    {#if step === "welcome"}
      <h1 class="mark">fenixOS</h1>
      <p>A small operating system for your ESP32 display board.</p>
    {:else if step === "looking"}
      <h1>Looking for your board</h1>
      <p>A board is plugged in. Connecting to it…</p>
    {:else if step === "plug"}
      <h1>Plug in your board</h1>
      <p>Windows needs the CH340 USB driver for this board. Use a USB-A to USB-C or micro-USB cable that carries data.</p>
      <p><a href="https://www.wch-ic.com/downloads/CH341SER_EXE.html">Get the CH340 driver from WCH</a></p>
    {:else if step === "checking"}
      <h1>Board found</h1>
      <p>Checking what runs on it…</p>
    {:else if step === "confirm"}
      <h1>Install fenixOS on this board?</h1>
      <p>This erases what is on it.</p>
      {#if probe}<p class="fine">{probe.chip.toUpperCase()} · {probe.flash} flash · {snap?.silent[0]?.port ?? ""}</p>{/if}
      <div class="buttons">
        <button type="button" class="primary" onclick={() => install(true)}>Install fenixOS</button>
        <button type="button" class="secondary" onclick={skip}>Not now</button>
      </div>
    {:else if step === "outdated"}
      <h1>Update fenixOS on this board?</h1>
      <p>The board runs fenixOS v{snap?.board?.firmware}. This app is v{snap?.app_version}. Your apps and settings stay.</p>
      <div class="buttons">
        <button type="button" class="primary" onclick={() => install(false)}>Update board</button>
        <button type="button" class="secondary" onclick={skip}>Not now</button>
      </div>
    {:else if step === "installing"}
      <h1>Installing fenixOS</h1>
      <p>
        {u?.stage === "erasing" ? "Erasing the board…" : u?.stage === "restarting" ? "Restarting the board…" : `Writing fenixOS… ${percent}%`}
      </p>
      <p class="fine">Keep the board plugged in.</p>
    {:else if step === "failed"}
      <h1>That did not work</h1>
      <p>{error || "The board did not answer."} Keep the board plugged in and try again.</p>
      <div class="buttons">
        <button type="button" class="primary" onclick={() => { error = ""; scene?.setPose("plugged"); step = "checking"; later(600, firmwareCheck); }}>Try again</button>
        <button type="button" class="secondary" onclick={skip}>Not now</button>
      </div>
    {:else if step === "done"}
      <h1>All set</h1>
      <p>fenixOS is running on your board.</p>
      <div class="buttons">
        <button type="button" class="primary" onclick={skip}>Open fenixOS</button>
      </div>
    {/if}
  </section>
{/key}

<style>
  :global(body) {
    background: #000;
    overflow: hidden;
  }
  canvas {
    position: fixed;
    inset: 0;
    width: 100%;
    height: 100%;
    display: block;
  }
  header {
    position: fixed;
    top: 0;
    left: 0;
    right: 0;
    height: 40px;
    display: flex;
    justify-content: flex-end;
    align-items: center;
    z-index: 2;
  }
  .fps {
    position: fixed;
    right: 16px;
    bottom: 12px;
    font-size: 13px;
    z-index: 2;
  }
  .skip {
    height: 28px;
    border: 0;
    border-radius: 14px;
    padding: 0 14px;
    margin-right: 8px;
    background: rgba(255, 255, 255, 0.08);
    color: var(--label-2);
    font-size: 13px;
  }
  .ctl {
    width: 46px;
    height: 40px;
    border: 0;
    background: transparent;
    display: flex;
    align-items: center;
    justify-content: center;
  }
  .ctl:hover {
    background: rgba(255, 255, 255, 0.08);
  }
  .copy {
    position: fixed;
    left: 64px;
    bottom: 64px;
    max-width: 440px;
    display: flex;
    flex-direction: column;
    gap: 8px;
    z-index: 1;
    animation: fade 600ms cubic-bezier(0.2, 0.8, 0.2, 1) both;
  }
  .copy.center {
    left: 0;
    right: 0;
    bottom: 72px;
    max-width: none;
    align-items: center;
    text-align: center;
  }
  @keyframes fade {
    from {
      opacity: 0;
      transform: translateY(8px);
    }
    to {
      opacity: 1;
      transform: none;
    }
  }
  @media (prefers-reduced-motion: reduce) {
    .copy {
      animation: fadeonly 250ms ease both;
    }
    @keyframes fadeonly {
      from {
        opacity: 0;
      }
      to {
        opacity: 1;
      }
    }
  }
  h1 {
    font-size: 34px;
    font-weight: 700;
    letter-spacing: -0.01em;
  }
  .mark {
    font-size: 56px;
  }
  p {
    font-size: 17px;
    line-height: 1.45;
    color: var(--label-2);
  }
  .fine {
    font-size: 13px;
  }
  .buttons {
    margin-top: 12px;
    display: flex;
    gap: 8px;
  }
  .primary,
  .secondary {
    height: 44px;
    border: 0;
    border-radius: 22px;
    padding: 0 22px;
    font-size: 17px;
  }
  .primary {
    background: var(--accent-fill);
    color: #fff;
    font-weight: 600;
  }
  .secondary {
    background: rgba(255, 255, 255, 0.08);
    color: var(--accent);
  }
</style>
