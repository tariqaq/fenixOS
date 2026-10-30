<script lang="ts">
  // Task manager (P27): the board's load, live. While the page is open it asks the board for a
  // SYSINFO every second; CPU load, thread CPU and link rates come from the change between two
  // answers. The board has threads (FreeRTOS tasks), not processes; apps run in the UI thread.
  import Icon from "../lib/Icon.svelte";
  import LineChart from "../lib/LineChart.svelte";
  import { getSysinfo, linkText, type Board, type Sysinfo, type TaskInfo } from "../lib/api";

  let { board, onreinstall }: { board: Board; onreinstall: () => void } = $props();

  const LEN = 60;
  // The USB link: 921,600 baud, 10 bits a byte.
  const LINK_BPS = 92160;
  const SERIES_1 = "var(--chart-1)";
  const SERIES_2 = "var(--chart-2)";

  let cur = $state<Sysinfo | null>(null);
  let unsupported = $state(false);
  let cores = $state<number[][]>([[], []]);
  let heap = $state<number[]>([]);
  let toBoard = $state<number[]>([]);
  let fromBoard = $state<number[]>([]);
  let threads = $state<{ t: TaskInfo; cpu: number | null }[]>([]);

  const push = (a: number[], v: number) => [...a, v].slice(-LEN);
  const wrap = (a: number, b: number) => (b - a) >>> 0;

  function take(s: Sysinfo) {
    const prev = cur;
    cur = s;
    heap = push(heap, s.heap_total - s.heap_free);
    if (!prev) {
      threads = s.tasks.map((t) => ({ t, cpu: null }));
      return;
    }
    const dt = wrap(prev.time_us, s.time_us);
    if (!dt) return;
    const before = new Map(prev.tasks.map((t) => [t.id, t.runtime]));
    // Per core: whatever its idle task did not use.
    cores = [0, 1].map((c) => {
      const idle = s.tasks.find((t) => t.name.startsWith("IDLE") && t.core === c);
      const was = idle && before.get(idle.id);
      const load = idle && was !== undefined ? 100 * (1 - wrap(was, idle.runtime) / dt) : null;
      return push(cores[c], load === null ? NaN : Math.max(0, Math.min(100, load)));
    });
    const secs = dt / 1e6;
    toBoard = push(toBoard, wrap(prev.rx_bytes, s.rx_bytes) / secs);
    fromBoard = push(fromBoard, wrap(prev.tx_bytes, s.tx_bytes) / secs);
    // Thread CPU as a share of both cores, like a desktop task manager.
    threads = s.tasks
      .map((t) => {
        const was = before.get(t.id);
        return { t, cpu: was === undefined ? null : (100 * wrap(was, t.runtime)) / (dt * 2) };
      })
      .sort((a, b) => (b.cpu ?? -1) - (a.cpu ?? -1) || a.t.name.localeCompare(b.t.name));
  }

  $effect(() => {
    let alive = true;
    async function poll() {
      const v = await getSysinfo().catch(() => null);
      if (!alive || !v) return;
      unsupported = v.unsupported;
      if (v.sample && v.sample.time_us !== cur?.time_us) take(v.sample);
    }
    poll();
    const t = setInterval(poll, 1000);
    return () => {
      alive = false;
      clearInterval(t);
    };
  });

  const nan = (a: number[]) => a.map((v) => (Number.isNaN(v) ? null : v));
  const last = (a: number[]) => (a.length && !Number.isNaN(a[a.length - 1]) ? a[a.length - 1] : null);
  const pct = (v: number) => `${Math.round(v)}%`;
  function bytes(n: number) {
    if (n < 1024) return `${Math.round(n)} B`;
    if (n < 1024 * 1024) return `${(n / 1024).toFixed(n < 10240 ? 1 : 0)} KB`;
    return `${(n / 1024 / 1024).toFixed(2)} MB`;
  }
  const rate = (n: number) => `${bytes(n)}/s`;
  const share = (used: number, total: number) => (total ? Math.min(100, (100 * used) / total) : 0);
  const STATES = ["Running", "Ready", "Waiting", "Suspended", "Ended"];
  const coreName = (c: number) => (c === 255 ? "Either" : `${c}`);

  const total = $derived.by(() => {
    const a = last(cores[0]);
    const b = last(cores[1]);
    return a === null || b === null ? null : (a + b) / 2;
  });
  const flags = $derived(cur?.flags ?? 0);
  // The session's transport (P37): USB, or TCP over Wi-Fi (no line rate to fill).
  const wifi = $derived(board.port.startsWith("tcp:"));
  const linkName = $derived(wifi ? "Wi-Fi link" : "USB link");
  const screen = $derived(flags & 2 ? "Panel resting" : flags & 1 ? "Dimmed" : "On");
</script>

<div class="wrap">
  <div class="head">
    <h1>Task manager</h1>
    <p class="sub">{linkText(board.port)}{unsupported ? "" : " · live, every second"}</p>
  </div>

  {#if unsupported}
    <div class="group">
      <div class="row">
        <Icon name="warning" size={18} color="var(--warning)" />
        <p>fenixOS v{board.firmware} on the board does not report its load. Version 0.1.5 and later do.</p>
      </div>
      <button type="button" class="row" onclick={onreinstall}>
        <Icon name="download-simple" size={18} color="var(--accent)" />
        <span>Reinstall firmware</span>
      </button>
    </div>
  {:else if !cur}
    <p class="sub">Waiting for the board…</p>
  {:else}
    <div class="grid">
      <section class="card wide">
        <div class="title">
          <h2>CPU</h2>
          <p class="sub">ESP32, 2 cores at {cur.cpu_mhz} MHz</p>
          <p class="hero">{total === null ? "–" : pct(total)}</p>
        </div>
        <div class="pair">
          {#each [0, 1] as c (c)}
            <div>
              <p class="lbl">Core {c}<span class="num">{last(cores[c]) === null ? "–" : pct(last(cores[c])!)}</span></p>
              <LineChart
                label="Core {c} load, last 60 seconds"
                series={[{ label: `Core ${c}`, color: "var(--accent)", values: nan(cores[c]) }]}
                max={100}
                format={pct}
              />
            </div>
          {/each}
        </div>
      </section>

      <section class="card">
        <div class="title">
          <h2>Memory</h2>
          <p class="sub">Internal RAM (heap)</p>
          <p class="hero">{bytes(cur.heap_total - cur.heap_free)}<span class="of">of {bytes(cur.heap_total)}</span></p>
        </div>
        <LineChart
          label="Heap in use, last 60 seconds"
          series={[{ label: "In use", color: "var(--accent)", values: heap }]}
          max={cur.heap_total}
          format={bytes}
        />
        <div class="facts">
          <p>Free<span>{bytes(cur.heap_free)}</span></p>
          <p>Lowest free since start<span>{bytes(cur.heap_min)}</span></p>
          <p>Largest free block<span>{bytes(cur.heap_largest)}</span></p>
          <p>Free for DMA<span>{bytes(cur.dma_free)}</span></p>
        </div>
      </section>

      <section class="card">
        <div class="title">
          <h2>{linkName}</h2>
          <p class="sub">{wifi ? `TCP to ${board.port.slice(4)}` : "Serial over USB, 921,600 baud"}</p>
          <p class="hero">{rate((last(toBoard) ?? 0) + (last(fromBoard) ?? 0))}</p>
        </div>
        <div class="legend">
          <span><i style="background: {SERIES_1}"></i>To board {rate(last(toBoard) ?? 0)}</span>
          <span><i style="background: {SERIES_2}"></i>From board {rate(last(fromBoard) ?? 0)}</span>
        </div>
        <LineChart
          label="{linkName} traffic, last 60 seconds"
          series={[
            { label: "To board", color: SERIES_1, values: toBoard },
            { label: "From board", color: SERIES_2, values: fromBoard },
          ]}
          format={rate}
        />
        <div class="facts">
          {#if !wifi}
            <p>Line in use<span>{share((last(toBoard) ?? 0) + (last(fromBoard) ?? 0), LINK_BPS).toFixed(1)}%</span></p>
          {/if}
          <p>Messages received<span>{cur.rx_frames.toLocaleString()}</span></p>
          <p>Damaged messages<span>{cur.rx_errors.toLocaleString()}</span></p>
        </div>
      </section>

      <section class="card">
        <div class="title">
          <h2>Storage</h2>
          <p class="sub">Flash chip, {bytes(cur.flash_size)}</p>
        </div>
        {#each [
          { name: "Firmware", used: cur.app_size, total: cur.app_slot, unit: bytes },
          { name: "Apps", used: cur.apps_used, total: cur.apps_total, unit: bytes },
          { name: "Settings", used: cur.nvs_used, total: cur.nvs_total, unit: (n: number) => `${n} entries` },
        ] as d (d.name)}
          <div class="meter">
            <p class="lbl">{d.name}<span class="num">{d.unit(d.used)} of {d.unit(d.total)}</span></p>
            <div class="track" role="meter" aria-label={d.name} aria-valuenow={d.used} aria-valuemin={0} aria-valuemax={d.total}>
              <div class="fill" style="transform: scaleX({share(d.used, d.total) / 100})"></div>
            </div>
          </div>
        {/each}
      </section>

      <section class="card">
        <div class="title">
          <h2>Display</h2>
          <p class="sub">2.8" ILI9341, 320 × 240</p>
        </div>
        <div class="facts">
          <p>Screen<span>{screen}</span></p>
          <p>Backlight<span>{pct((100 * cur.backlight) / 255)}</span></p>
          <p>Animation<span>{(cur.fps_x10 / 10).toFixed(1)} fps</span></p>
          <p>Burn-in protection<span>{flags & 4 ? "On" : "Off"}</span></p>
          <p>Streaming to this PC<span>{flags & 8 ? "Yes" : "No"}</span></p>
        </div>
      </section>

      <section class="card">
        <div class="title">
          <h2>Wireless</h2>
          <p class="sub">Change it in Settings</p>
        </div>
        <div class="facts">
          <p class="radio"><Icon name={flags & 16 ? "wifi-high" : "wifi-slash"} size={16} color="var(--label-2)" />Wi-Fi<span>{flags & 16 ? "On" : "Off"}</span></p>
        </div>
      </section>

      <section class="card wide">
        <div class="title">
          <h2>Threads</h2>
          <p class="sub">{cur.tasks.length} FreeRTOS tasks{board.app_id ? ` · app ${board.app_id} runs in main` : ""}</p>
        </div>
        <table>
          <thead>
            <tr><th>Name</th><th>Core</th><th>Priority</th><th>State</th><th class="r">CPU</th><th class="r">Stack free</th></tr>
          </thead>
          <tbody>
            {#each threads as { t, cpu } (t.id)}
              <tr>
                <td>{t.name}</td>
                <td>{coreName(t.core)}</td>
                <td>{t.priority}</td>
                <td>{STATES[t.state] ?? t.state}</td>
                <td class="r">{cpu === null ? "–" : cpu < 0.1 && cpu > 0 ? "<0.1%" : `${cpu.toFixed(1)}%`}</td>
                <td class="r">{bytes(t.stack_free)}</td>
              </tr>
            {/each}
          </tbody>
        </table>
        <p class="note-s">CPU is a share of both cores. IDLE0 and IDLE1 run when a core has nothing else to do. Stack free is the least free stack since the task started.</p>
      </section>
    </div>
  {/if}
</div>

<style>
  .wrap {
    max-width: 1000px;
    margin: 0 auto;
    display: flex;
    flex-direction: column;
    gap: 20px;
  }
  .head {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }
  .sub {
    font-size: 13px;
    color: var(--label-2);
  }
  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
    gap: 16px;
  }
  .card {
    background: var(--surface-1);
    border-radius: var(--radius-pc-card);
    padding: 16px;
    display: flex;
    flex-direction: column;
    gap: 12px;
    min-width: 0;
  }
  .wide {
    grid-column: 1 / -1;
  }
  .title {
    display: grid;
    grid-template-columns: 1fr auto;
    column-gap: 12px;
  }
  .title h2 {
    font-size: 17px;
    font-weight: 600;
  }
  .title .sub {
    grid-column: 1;
  }
  .hero {
    grid-column: 2;
    grid-row: 1 / span 2;
    align-self: center;
    font-size: 28px;
    font-weight: 600;
    font-variant-numeric: tabular-nums;
  }
  /* Narrow cards: the big number goes under the title. */
  .card:not(.wide) .hero {
    grid-column: 1;
    grid-row: auto;
    margin-top: 6px;
  }
  .of {
    margin-left: 6px;
    font-size: 15px;
    font-weight: 400;
    color: var(--label-2);
  }
  .pair {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
    gap: 20px;
  }
  .lbl {
    display: flex;
    font-size: 13px;
    margin-bottom: 6px;
  }
  .num {
    margin-left: auto;
    color: var(--label-2);
    font-variant-numeric: tabular-nums;
  }
  .facts p {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 14px;
    padding: 7px 0;
    border-top: 1px solid var(--separator);
  }
  .facts span {
    margin-left: auto;
    color: var(--label-2);
    font-variant-numeric: tabular-nums;
  }
  .legend {
    display: flex;
    flex-wrap: wrap;
    gap: 16px;
    font-size: 13px;
    color: var(--label-2);
    font-variant-numeric: tabular-nums;
  }
  .legend span {
    display: flex;
    align-items: center;
    gap: 6px;
  }
  .legend i {
    width: 10px;
    height: 10px;
    border-radius: 2px;
  }
  .track {
    height: 8px;
    border-radius: 4px;
    background: var(--surface-3);
    overflow: hidden;
  }
  /* Scaled, not resized, so the change animates on the compositor; the track clips the ends. */
  .fill {
    height: 100%;
    background: var(--accent);
    transform-origin: left;
    transition: transform var(--dur-base) var(--ease);
  }
  table {
    width: 100%;
    border-collapse: collapse;
    font-size: 14px;
    font-variant-numeric: tabular-nums;
  }
  th {
    text-align: left;
    font-weight: 400;
    font-size: 12px;
    color: var(--label-2);
    padding: 0 8px 6px 0;
  }
  td {
    padding: 7px 8px 7px 0;
    border-top: 1px solid var(--separator);
    white-space: nowrap;
  }
  td:not(:first-child) {
    color: var(--label-2);
  }
  .r {
    text-align: right;
    padding-right: 0;
  }
  .note-s {
    font-size: 12px;
    line-height: 1.5;
    color: var(--label-2);
  }
</style>
