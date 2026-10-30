<script lang="ts">
  // Sidebar (PcDashboard and siblings): Apps, Board, Task manager (P27; artboard PcTaskManager since P35), Settings,
  // and the board status card. The selection is one highlight that glides to the chosen item
  // (250 ms).
  import Icon from "./Icon.svelte";
  import type { IconName } from "./icons";
  import { linkText, type Board, type Page } from "./api";

  let {
    page = $bindable(),
    board,
    onreselect,
  }: { page: Page; board: Board | null; onreselect?: () => void } = $props();

  const items: { id: Page; label: string; icon: IconName }[] = [
    { id: "apps", label: "Apps", icon: "squares-four" },
    { id: "board", label: "Board", icon: "cpu" },
    { id: "tasks", label: "Task manager", icon: "pulse" },
    { id: "settings", label: "Settings", icon: "gear-six" },
  ];
  const index = $derived(Math.max(0, items.findIndex((it) => it.id === page)));
</script>

<nav aria-label="fenixOS">
  <span class="pill" aria-hidden="true" style="transform: translateY({index * 40}px)"></span>
  {#each items as it (it.id)}
    <button
      type="button"
      class:current={page === it.id}
      aria-current={page === it.id ? "page" : undefined}
      onclick={() => (page === it.id ? onreselect?.() : (page = it.id))}
    >
      <Icon name={it.icon} size={18} color={page === it.id ? "var(--accent)" : "var(--label-2)"} />
      <span>{it.label}</span>
    </button>
  {/each}
  <div class="status">
    <span class="status-icon"><Icon
        name={board?.port.startsWith("tcp:") ? "wifi-high" : "usb"}
        size={20}
        color={board ? "var(--success)" : "var(--label-2)"}
      /></span>
    <div class="lines">
      <p class="title">{board ? "Board connected" : "Board not connected"}</p>
      {#if board}
        <p>{linkText(board.port)}</p>
        <p>fenixOS v{board.firmware}</p>
      {:else}
        <p>Looking for the board</p>
      {/if}
    </div>
  </div>
</nav>

<style>
  nav {
    position: relative;
    flex: 1 1 220px;
    max-width: 260px;
    background: var(--surface-1);
    padding: 16px 12px;
    display: flex;
    flex-direction: column;
    gap: 4px;
  }
  button {
    position: relative;
    height: 36px;
    border: 0;
    border-radius: 8px;
    background: transparent;
    color: var(--label);
    padding: 0 10px;
    display: flex;
    align-items: center;
    gap: 10px;
    font-size: 15px;
    text-align: left;
  }
  /* Buttons are 36 px with 4 px gaps: item i sits 40 px x i below the first. */
  .pill {
    position: absolute;
    top: 16px;
    left: 12px;
    right: 12px;
    height: 36px;
    border-radius: 8px;
    background: var(--surface-2);
    transition: transform var(--dur-base) var(--ease);
  }
  button:not(.current):hover {
    background: rgba(255, 255, 255, 0.04);
  }
  button.current {
    font-weight: 600;
  }
  .status {
    margin-top: auto;
    background: var(--surface-2);
    border-radius: 12px;
    padding: 12px;
    display: flex;
    gap: 10px;
    align-items: flex-start;
  }
  .status-icon {
    flex: 0 0 20px;
    margin-top: 1px;
  }
  .lines {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }
  .lines p {
    font-size: 12px;
    color: var(--label-3);
  }
  .lines p.title {
    font-size: 14px;
    font-weight: 600;
    color: var(--label);
  }
</style>
