<script lang="ts">
  // Artboard 9 (PcAppSettings): one app's switch, its own settings rendered from the manifest
  // (every app looks the same), permissions, source and version, Remove app.
  import Icon from "../lib/Icon.svelte";
  import AppIcon from "../lib/AppIcon.svelte";
  import Switch from "../lib/Switch.svelte";
  import {
    appButton,
    permissionRows,
    removeApp,
    setAppEnabled,
    setAppSetting,
    tileColor,
    type AppView,
    type Setting,
  } from "../lib/api";
  import { FAST, enter } from "../lib/motion";

  let { app, onback }: { app: AppView; onback: () => void } = $props();

  const perms = $derived(permissionRows(app.permissions, app.has_pc, app.elevated));
  const source = $derived(app.repo.replace(/^https:\/\//, ""));
  const value = (s: Setting) => app.values[s.key] ?? s.default;

  let confirming = $state(false);

  // Text settings are sent when the field loses focus or Enter is pressed, not on every key.
  function commitText(s: Setting, e: Event) {
    const v = (e.target as HTMLInputElement).value.slice(0, s.max_len ?? 100);
    if (v !== value(s)) setAppSetting(app.id, s.key, v);
  }
</script>

<div class="wrap">
  <button type="button" class="back" onclick={onback}>
    <Icon name="caret-left" size={14} color="var(--accent)" />
    <span>Apps</span>
  </button>

  <div class="head">
    <div class="badge" style="background: {tileColor(app.color)}">
      <AppIcon name={app.icon} size={32} />
    </div>
    <div class="title">
      <h1>{app.name}</h1>
      <p>{app.description}</p>
    </div>
    <div class="onoff">
      <span id="onoff-{app.id}">On for board</span>
      <Switch checked={app.enabled} labelledby="onoff-{app.id}" onchange={(on) => setAppEnabled(app.id, on)} />
    </div>
  </div>
  {#if app.enabled && app.status !== "on"}
    <p class="state" class:problem={app.status === "error"} in:enter={{ y: -4, duration: FAST }}>{app.detail}</p>
  {/if}

  {#if app.settings.length}
    <section>
      <h2>On the board</h2>
      <div class="group">
        {#each app.settings as s (s.key)}
          <div class="row">
            {#if s.type === "toggle"}
              <span id="set-{s.key}">{s.label}</span>
              <span class="end">
                <Switch
                  checked={value(s) === true}
                  labelledby="set-{s.key}"
                  onchange={(v) => setAppSetting(app.id, s.key, v)}
                />
              </span>
            {:else if s.type === "choice"}
              <label for="set-{s.key}">{s.label}</label>
              <select
                id="set-{s.key}"
                class="end"
                value={value(s)}
                onchange={(e) => setAppSetting(app.id, s.key, (e.target as HTMLSelectElement).value)}
              >
                {#each s.options ?? [] as o (o.value)}
                  <option value={o.value}>{o.label}</option>
                {/each}
              </select>
            {:else if s.type === "slider"}
              <label for="set-{s.key}">{s.label}</label>
              <span class="end slider">
                <input
                  id="set-{s.key}"
                  type="range"
                  min={s.min}
                  max={s.max}
                  step={s.step}
                  value={value(s)}
                  onchange={(e) => setAppSetting(app.id, s.key, Number((e.target as HTMLInputElement).value))}
                />
                <span class="num">{value(s)}{s.unit ? ` ${s.unit}` : ""}</span>
              </span>
            {:else if s.type === "text"}
              <label for="set-{s.key}">{s.label}</label>
              <input
                id="set-{s.key}"
                class="end text"
                type="text"
                maxlength={s.max_len ?? 100}
                placeholder={s.placeholder ?? ""}
                value={value(s)}
                onchange={(e) => commitText(s, e)}
              />
            {:else if s.type === "button"}
              <button type="button" class="action" onclick={() => appButton(app.id, s.key)}>{s.label}</button>
            {/if}
          </div>
        {/each}
      </div>
      <p class="foot">These options come from the app. fenixOS shows every app's options in this same style.</p>
    </section>
  {/if}

  {#if perms.length}
    <section>
      <h2>Permissions</h2>
      <div class="group">
        {#each perms as p (p.key)}
          <div class="row">
            <AppIcon name={p.icon} size={20} color="var(--label-2)" />
            <span>{p.text}</span>
          </div>
        {/each}
      </div>
    </section>
  {/if}

  <section>
    <h2>App</h2>
    <dl class="group facts">
      <div class="row"><dt>Source</dt><dd><a href={app.repo}>{source}</a></dd></div>
      <div class="row"><dt>Version</dt><dd>{app.version}</dd></div>
    </dl>
  </section>

  {#if confirming}
    <div class="confirm" in:enter={{ y: -6 }}>
      <p>Remove {app.name} from this PC and the board? Its settings are deleted too.</p>
      <div class="buttons">
        <button type="button" class="remove" onclick={() => removeApp(app.id)}>Remove</button>
        <button type="button" class="cancel" onclick={() => (confirming = false)}>Cancel</button>
      </div>
    </div>
  {:else}
    <button type="button" class="remove" onclick={() => (confirming = true)}>Remove app</button>
  {/if}
</div>

<style>
  .wrap {
    max-width: 760px;
    margin: -8px auto 0 auto;
    display: flex;
    flex-direction: column;
    gap: 22px;
  }
  .back {
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
  .head {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 16px;
  }
  .badge {
    width: 64px;
    height: 64px;
    flex: 0 0 64px;
    border-radius: 16px;
    display: flex;
    align-items: center;
    justify-content: center;
  }
  .title {
    flex: 1;
    min-width: 200px;
    display: flex;
    flex-direction: column;
    gap: 2px;
  }
  .title h1 {
    font-size: 28px;
  }
  .title p {
    font-size: 14px;
    color: var(--label-2);
  }
  .onoff {
    display: flex;
    align-items: center;
    gap: 10px;
    font-size: 15px;
  }
  .state {
    margin-top: -10px;
    font-size: 13px;
    color: var(--label-2);
  }
  .state.problem {
    color: var(--warning);
  }
  section {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }
  h2 {
    margin-left: 4px;
    font-size: 15px;
    font-weight: 600;
    color: var(--label-2);
  }
  .row {
    min-height: 48px;
    padding: 0 16px;
  }
  .end {
    margin-left: auto;
  }
  select,
  input.text {
    height: 30px;
    border: 0;
    border-radius: 8px;
    background: var(--surface-2);
    color: var(--label);
    padding: 0 10px;
    font-family: inherit;
    font-size: 14px;
  }
  input.text {
    width: 240px;
    user-select: text;
  }
  select:focus-visible,
  input:focus-visible {
    outline: 2px solid var(--accent);
    outline-offset: 2px;
  }
  .slider {
    display: flex;
    align-items: center;
    gap: 10px;
  }
  .slider input {
    width: 200px;
    accent-color: var(--accent);
  }
  .num {
    min-width: 48px;
    text-align: right;
    font-size: 14px;
    color: var(--label-2);
  }
  .action {
    border: 0;
    background: transparent;
    padding: 0;
    color: var(--accent);
    font-size: 15px;
  }
  .foot {
    margin-left: 4px;
    font-size: 12px;
    color: var(--label-2);
  }
  .facts {
    margin: 0;
  }
  .facts dt {
    font-size: 15px;
  }
  .facts dd {
    margin: 0 0 0 auto;
    color: var(--label-2);
  }
  .facts dd a {
    color: var(--label-2);
  }
  .remove {
    align-self: flex-start;
    height: 40px;
    border: 0;
    border-radius: var(--radius-pc-button);
    background: var(--surface-1);
    color: var(--danger);
    padding: 0 16px;
    font-size: 15px;
    font-weight: 600;
  }
  .confirm {
    background: var(--surface-1);
    border-radius: var(--radius-pc-card);
    padding: 16px;
    display: flex;
    flex-direction: column;
    gap: 12px;
    font-size: 15px;
  }
  .buttons {
    display: flex;
    gap: 8px;
  }
  .confirm .remove {
    background: var(--danger-fill);
    color: #fff;
  }
  .cancel {
    height: 40px;
    border: 0;
    border-radius: var(--radius-pc-button);
    background: transparent;
    color: var(--accent);
    padding: 0 16px;
    font-size: 15px;
  }
</style>
