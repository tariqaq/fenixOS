<script lang="ts">
  // Artboard 8 (PcDashboard). P7 has no app manager yet, so this is the empty state; tiles,
  // switches and "Add app" arrive with the registry in P8.
  import Icon from "../lib/Icon.svelte";
</script>

<div class="wrap">
  <div class="head">
    <h1>Apps</h1>
    <button type="button" class="add" disabled title="Comes with the app registry">
      <Icon name="plus" size={14} color="#FFFFFF" />
      <span>Add app</span>
    </button>
  </div>
  <div class="empty">
    <Icon name="puzzle-piece" size={40} color="var(--label-2)" />
    <p class="title">No apps yet</p>
    <p class="text">Add an app from the fenixOS registry. It shows up here, and on the board once you switch it on.</p>
  </div>
</div>

<style>
  .wrap {
    max-width: 1120px;
    margin: 0 auto;
    display: flex;
    flex-direction: column;
    gap: 24px;
  }
  .head {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
  }
  .add {
    height: 36px;
    border: 0;
    border-radius: var(--radius-pc-button);
    background: var(--accent-fill);
    color: #fff;
    padding: 0 14px;
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 15px;
    font-weight: 600;
  }
  .add:disabled {
    opacity: 0.5;
  }
  .empty {
    background: var(--surface-1);
    border-radius: var(--radius-pc-tile);
    padding: 48px 24px;
    display: flex;
    flex-direction: column;
    align-items: center;
    text-align: center;
    gap: 10px;
  }
  .title {
    margin-top: 6px;
    font-size: 22px;
    font-weight: 600;
  }
  .text {
    max-width: 420px;
    font-size: 15px;
    line-height: 1.5;
    color: var(--label-2);
  }
</style>
