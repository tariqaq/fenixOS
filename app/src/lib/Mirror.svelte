<script lang="ts">
  // Live board screen (PcBoard, artboard 11). Off until the user starts streaming in the board's
  // Stream app (P13–P15); `live` is the engine's `streaming`, which the board keeps in sync. It can
  // be stopped here, and keeps running when the page is left. While live it polls the engine for frames at
  // up to 10 fps. Clicks on the picture are sent to the board as touches.
  import { onDestroy } from "svelte";
  import { mirrorFrame, mirrorSet, mirrorTouch } from "./api";

  let { live }: { live: boolean } = $props();
  let canvas: HTMLCanvasElement;
  let fps = $state(0);
  let size = $state({ w: 320, h: 240 });
  let version = 0;
  const shown: number[] = [];
  let timer: ReturnType<typeof setTimeout> | undefined;
  let alive = true;
  let run = 0; // one poll loop per stream start; a loop from an earlier start ends itself

  async function poll(r: number) {
    if (!alive || r !== run) return;
    const t0 = performance.now();
    try {
      const buf = await mirrorFrame(version);
      const head = new DataView(buf, 0, 16);
      const v = Number(head.getBigUint64(0, true));
      const w = head.getUint16(8, true);
      const h = head.getUint16(10, true);
      if (buf.byteLength >= 16 + w * h * 2 && v !== version) {
        version = v;
        draw(new Uint16Array(buf, 16, w * h), w, h);
        shown.push(performance.now());
      }
      // fps = new pictures shown in the last 2 s (a static screen shows 0).
      const now = performance.now();
      while (shown.length && now - shown[0] > 2000) shown.shift();
      fps = shown.length / 2;
    } catch {
      // Board gone: keep polling; the page is replaced by the finding-the-board screen anyway.
    }
    if (r === run) timer = setTimeout(() => poll(r), Math.max(0, 100 - (performance.now() - t0)));
  }

  function draw(px: Uint16Array, w: number, h: number) {
    if (size.w !== w || size.h !== h) size = { w, h };
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
    }
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    const img = ctx.createImageData(w, h);
    const d = img.data;
    for (let i = 0, j = 0; i < px.length; i++, j += 4) {
      const p = px[i];
      d[j] = ((p >> 11) & 31) * 255 / 31;
      d[j + 1] = ((p >> 5) & 63) * 255 / 63;
      d[j + 2] = (p & 31) * 255 / 31;
      d[j + 3] = 255;
    }
    ctx.putImageData(img, 0, 0);
  }

  $effect(() => {
    if (!live) return;
    const r = ++run;
    poll(r);
    return () => {
      run++;
      clearTimeout(timer);
    };
  });
  const stop = () => mirrorSet(false);

  // Board coordinates of a pointer event on the scaled picture.
  function at(e: PointerEvent): [number, number] {
    const r = canvas.getBoundingClientRect();
    const x = Math.round(((e.clientX - r.left) / r.width) * size.w);
    const y = Math.round(((e.clientY - r.top) / r.height) * size.h);
    return [Math.min(Math.max(x, 0), size.w - 1), Math.min(Math.max(y, 0), size.h - 1)];
  }
  // The board keeps only the latest touch state and reads it every few ms, so a release that
  // follows a press too fast would never be seen as a tap: hold each press for 100 ms at least.
  let pressed = false;
  let pressedAt = 0;
  let lastMove = 0;
  function down(e: PointerEvent) {
    if (!live) return;
    pressed = true;
    pressedAt = performance.now();
    canvas.setPointerCapture(e.pointerId);
    mirrorTouch(...at(e), true);
  }
  function move(e: PointerEvent) {
    if (!pressed || performance.now() - lastMove < 30) return;
    lastMove = performance.now();
    mirrorTouch(...at(e), true);
  }
  function up(e: PointerEvent) {
    if (!pressed) return;
    pressed = false;
    const [x, y] = at(e);
    setTimeout(() => mirrorTouch(x, y, false), Math.max(0, 100 - (performance.now() - pressedAt)));
  }

  onDestroy(() => {
    alive = false;
    clearTimeout(timer);
  });
</script>

<section class="mirror" aria-label="Live screen">
  <div class="frame">
    <div class="screen" style="aspect-ratio: {size.w} / {size.h}; width: {size.h > size.w ? '56%' : '100%'}">
      <canvas
        bind:this={canvas}
        width="320"
        height="240"
        onpointerdown={down}
        onpointermove={move}
        onpointerup={up}
        onpointercancel={up}
      ></canvas>
      {#if !live}
        <div class="off">
          <p>Streaming is off.</p>
          <p class="fine">On the board, open Stream and tap Start streaming.</p>
        </div>
      {/if}
    </div>
  </div>
  <div class="bar">
    <span class="chip">
      <span class="dot" class:off={!live}></span>
      <span>{live ? `Live, ${Math.round(fps)} fps` : "Off"}</span>
    </span>
    <p>Mirrors the board screen as it is now.</p>
    {#if live}
      <button type="button" onclick={stop}>
        <svg aria-hidden="true" width="12" height="12" viewBox="0 0 12 12"
          ><rect x="1" y="1" width="10" height="10" rx="2" fill="var(--danger)"></rect></svg
        >
        <span>Stop stream</span>
      </button>
    {/if}
  </div>
</section>

<style>
  .mirror {
    flex: 0 1 640px;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 12px;
  }
  .frame {
    padding: 10px;
    background: var(--surface-1);
    border-radius: var(--radius-pc-tile);
    max-width: 660px;
  }
  .screen {
    position: relative;
    border-radius: 10px;
    overflow: hidden;
    background: #000;
    margin: 0 auto;
  }
  .off {
    position: absolute;
    inset: 0;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 4px;
    text-align: center;
    pointer-events: none;
  }
  .off p {
    font-size: 15px;
    color: var(--label);
  }
  .off .fine {
    font-size: 13px;
    color: var(--label-2);
  }
  canvas {
    display: block;
    width: 100%;
    height: 100%;
    image-rendering: pixelated;
    touch-action: none;
  }
  .bar {
    display: flex;
    align-items: center;
    gap: 12px;
  }
  .chip {
    display: flex;
    align-items: center;
    gap: 8px;
    height: 28px;
    padding: 0 12px;
    border-radius: 14px;
    background: var(--surface-1);
    font-size: 14px;
    white-space: nowrap;
  }
  .dot {
    width: 8px;
    height: 8px;
    border-radius: 4px;
    background: var(--success);
  }
  .dot.off {
    background: var(--label-2);
  }
  .bar p {
    font-size: 14px;
    color: var(--label-2);
  }
  button {
    margin-left: auto;
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
    white-space: nowrap;
  }
</style>
