<script lang="ts">
  // Artboard 7 (PcConnect), in 3D since P11: the welcome's board model waits with the cable
  // hovering beside its USB-C port. When the board connects the plug seats, the model's screen
  // boots, and `ondone` asks App to fade over to the dashboard. The main window runs with the GPU
  // on since P11: software WebGL took ~8 cores here (docs/decisions.md).
  import { onDestroy, onMount, untrack } from "svelte";
  import type { BoardScene } from "../ootb/scene";
  import { showWelcome, type Silent } from "../lib/api";

  let {
    silent,
    connected,
    appVersion,
    ondone,
  }: { silent: Silent[]; connected: boolean; appVersion: string; ondone: () => void } = $props();

  const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  let canvas: HTMLCanvasElement;
  let scene = $state<BoardScene | undefined>();
  let timers: number[] = [];
  const later = (ms: number, f: () => void) => timers.push(window.setTimeout(f, reduced ? Math.min(ms, 300) : ms));
  const clear = () => (timers.forEach(clearTimeout), (timers = []));

  // What the scene shows: the cable waiting, seated (a board that is not fenixOS yet), or the
  // plug-in run that ends on the dashboard.
  type Stage = "waiting" | "seated" | "connecting";
  let stage = $state<Stage>("waiting");
  const present = $derived(connected ? "connecting" : silent.length ? "seated" : "waiting");
  // The silent board's port, kept: `silent` empties in the same update that connects the board,
  // while the "Board found" copy is still on screen (it read silent[0] and threw).
  let port = $state(untrack(() => silent[0]?.port ?? ""));
  $effect(() => {
    if (silent.length) port = silent[0].port;
  });

  let gone = false;
  onMount(() => {
    // three.js loads only when this screen shows, not with every dashboard start.
    import("../ootb/scene").then(({ BoardScene }) => {
      if (gone) return;
      const s = new BoardScene(canvas, reduced);
      s.version = appVersion;
      // First pose: turn in from the intro angle (a cut with reduced motion).
      stage = present === "waiting" ? "waiting" : "seated";
      s.setPose(stage === "waiting" ? "waiting" : "plugged");
      scene = s;
    });
    const resize = () => scene?.resize();
    window.addEventListener("resize", resize);
    return () => window.removeEventListener("resize", resize);
  });
  onDestroy(() => {
    gone = true;
    clear();
    scene?.dispose();
  });

  $effect(() => {
    const want = present;
    if (!scene || want === stage) return;
    clear();
    stage = want;
    if (want === "waiting") {
      scene.mode = "off";
      scene.setPose("waiting");
    } else if (want === "seated") {
      scene.mode = "off";
      scene.setPose("plugged");
    } else {
      // Plug seats (700 ms) with the port glow, the screen boots, then the dashboard.
      scene.setPose("plugged");
      later(800, () => scene && (scene.mode = "boot"));
      later(2600, ondone);
    }
  });
</script>

<div class="page">
  <canvas bind:this={canvas}></canvas>
  {#key stage}
    <section class="copy" aria-live="polite">
      {#if stage === "connecting"}
        <h1>Board connected</h1>
        <p>Opening fenixOS…</p>
      {:else if stage === "seated"}
        <h1>Board found</h1>
        <p>A board on {port} did not answer as fenixOS.</p>
        <p class="fine">It may have no fenixOS on it yet, or another firmware.</p>
        <button type="button" class="setup" onclick={() => showWelcome()}>Set up this board</button>
      {:else}
        <h1>Plug in your board</h1>
        <p>Connect the ESP32 board with a USB cable. fenixOS finds it on its own and connects.</p>
        <p class="fine">
          Not showing up? Windows needs the CH340 USB driver, and the cable must carry data.
          <a href="https://www.wch-ic.com/downloads/CH341SER_EXE.html" target="_blank" rel="noreferrer">Get the CH340 driver from WCH</a>
        </p>
      {/if}
    </section>
  {/key}
</div>

<style>
  .page {
    position: relative;
    flex: 1;
    min-height: 0;
    overflow: hidden;
    background: var(--bg);
  }
  canvas {
    position: absolute;
    inset: 0;
    width: 100%;
    height: 100%;
    display: block;
  }
  .copy {
    position: absolute;
    left: 48px;
    bottom: 48px;
    max-width: 420px;
    display: flex;
    flex-direction: column;
    gap: 8px;
    animation: fade 600ms var(--ease) both;
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
    font-size: 30px;
    font-weight: 700;
    line-height: 1.15;
    letter-spacing: -0.01em;
  }
  p {
    font-size: 15px;
    line-height: 1.5;
    color: var(--label-2);
  }
  .fine {
    font-size: 13px;
  }
  .setup {
    align-self: flex-start;
    margin-top: 8px;
    height: 44px;
    border: 0;
    border-radius: var(--radius-pc-button);
    padding: 0 20px;
    background: var(--accent-fill);
    color: var(--label);
    font-size: 15px;
    font-weight: 600;
  }
  .fine a {
    display: block;
    margin-top: 4px;
  }
</style>
