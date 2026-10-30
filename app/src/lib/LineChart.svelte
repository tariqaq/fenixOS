<script lang="ts">
  // Line chart for the Task manager: the last `len` seconds, newest on the right. 2 px lines on
  // hairline gridlines; one series gets a faint area. Hover shows a crosshair and the values at
  // that second. Text stays in label colours; the series colour is only on marks and keys.
  type Series = { label: string; color: string; values: (number | null)[] };
  let {
    series,
    len = 60,
    max = null,
    height = 96,
    format,
    label,
  }: {
    series: Series[];
    len?: number;
    /** Fixed top of the scale (100 for percentages); null scales to the data. */
    max?: number | null;
    height?: number;
    format: (v: number) => string;
    label: string;
  } = $props();

  let width = $state(0);
  let hover = $state<number | null>(null);
  const PAD = 2; // room for the 2 px line at the top and bottom

  const top = $derived.by(() => {
    if (max !== null) return max;
    let m = 0;
    for (const s of series) for (const v of s.values) if (v !== null && v > m) m = v;
    return m > 0 ? m * 1.15 : 1;
  });
  const x = (i: number, n: number) => width - ((n - 1 - i) * width) / (len - 1);
  const y = (v: number) => PAD + (height - 2 * PAD) * (1 - Math.min(v, top) / top);

  function path(values: (number | null)[]) {
    let d = "";
    let pen = false;
    values.forEach((v, i) => {
      if (v === null) return void (pen = false);
      d += `${pen ? "L" : "M"}${x(i, values.length).toFixed(1)},${y(v).toFixed(1)}`;
      pen = true;
    });
    return d;
  }
  function area(values: (number | null)[]) {
    const pts = values.map((v, i) => [i, v] as const).filter(([, v]) => v !== null) as [number, number][];
    if (pts.length < 2) return "";
    const n = values.length;
    const line = pts.map(([i, v]) => `${x(i, n).toFixed(1)},${y(v).toFixed(1)}`).join("L");
    return `M${x(pts[0][0], n).toFixed(1)},${height}L${line}L${x(pts[pts.length - 1][0], n).toFixed(1)},${height}Z`;
  }

  const n = $derived(series[0]?.values.length ?? 0);
  // The hovered column, counted from the newest sample (0 = now).
  function move(e: PointerEvent) {
    const r = (e.currentTarget as HTMLElement).getBoundingClientRect();
    const back = Math.round(((r.width - (e.clientX - r.left)) / r.width) * (len - 1));
    hover = back >= 0 && back < n ? back : null;
  }
  const hx = $derived(hover === null ? 0 : x(n - 1 - hover, n));
</script>

<div
  class="chart"
  style="height: {height}px"
  bind:clientWidth={width}
  role="img"
  aria-label={label}
  onpointermove={move}
  onpointerleave={() => (hover = null)}
>
  {#if width}
    <svg {width} {height} aria-hidden="true">
      {#each [0, 0.5, 1] as g (g)}
        <line class="grid" x1="0" x2={width} y1={y(top * g) + 0.5} y2={y(top * g) + 0.5} />
      {/each}
      {#if series.length === 1}
        <path d={area(series[0].values)} fill={series[0].color} opacity="0.14" />
      {/if}
      {#each series as s (s.label)}
        <path d={path(s.values)} fill="none" stroke={s.color} stroke-width="2" stroke-linejoin="round" stroke-linecap="round" />
      {/each}
      {#if hover !== null}
        <line class="cross" x1={hx} x2={hx} y1="0" y2={height} />
        {#each series as s (s.label)}
          {@const v = s.values[n - 1 - hover]}
          {#if v !== null && v !== undefined}
            <circle cx={hx} cy={y(v)} r="4" fill={s.color} stroke="var(--surface-1)" stroke-width="2" />
          {/if}
        {/each}
      {/if}
    </svg>
    {#if hover !== null}
      <div class="tip" class:left={hx > width / 2} style="left: {hx}px">
        <p class="when">{hover === 0 ? "Now" : `${hover} s ago`}</p>
        {#each series as s (s.label)}
          {@const v = s.values[n - 1 - hover]}
          <p>
            <span class="key" style="background: {s.color}"></span>
            {#if series.length > 1}<span class="name">{s.label}</span>{/if}
            <span class="v">{v === null || v === undefined ? "–" : format(v)}</span>
          </p>
        {/each}
      </div>
    {/if}
  {/if}
</div>
<div class="axis" aria-hidden="true"><span>{len} s</span><span>Now</span></div>

<style>
  .chart {
    position: relative;
    width: 100%;
  }
  svg {
    display: block;
    overflow: visible;
  }
  .grid {
    stroke: var(--separator);
    stroke-width: 1;
  }
  .cross {
    stroke: var(--label-2);
    stroke-width: 1;
  }
  .axis {
    margin-top: 4px;
    display: flex;
    justify-content: space-between;
    font-size: 11px;
    color: var(--label-2);
  }
  .tip {
    position: absolute;
    top: 0;
    transform: translateX(10px);
    background: var(--surface-3);
    border-radius: 8px;
    padding: 6px 8px;
    font-size: 12px;
    white-space: nowrap;
    pointer-events: none;
    box-shadow: var(--shadow-tip);
    z-index: 1;
  }
  .tip.left {
    transform: translateX(calc(-100% - 10px));
  }
  .tip p {
    display: flex;
    align-items: center;
    gap: 6px;
    line-height: 1.5;
  }
  .when {
    color: var(--label-2);
  }
  .key {
    width: 8px;
    height: 8px;
    border-radius: 2px;
  }
  .name {
    color: var(--label-2);
  }
  .v {
    margin-left: auto;
    font-variant-numeric: tabular-nums;
  }
</style>
