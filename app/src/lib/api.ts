// The UI's view of the engine (app/src-tauri/src/engine.rs). The window holds no state of its
// own: it reads a snapshot on start and then follows the engine's events.
import { invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";

export interface Board {
  port: string;
  firmware: string;
  proto: number;
  width: number;
  height: number;
  rotation: number;
  mac: string;
  /** When the board booted, Unix ms. */
  booted_at_ms: number;
  free_heap: number;
  min_heap: number;
  fps_x10: number;
  apps_used_kb: number;
  apps_total_kb: number;
  screen: number;
  app_id: string;
}

export interface Silent {
  port: string;
  detail: string;
}

export interface Setting {
  key: string;
  type: "toggle" | "slider" | "choice" | "text" | "button";
  label: string;
  default?: unknown;
  min?: number;
  max?: number;
  step?: number;
  unit?: string;
  options?: { value: string; label: string }[];
  placeholder?: string;
  max_len?: number;
}

/** An added app (engine `AppView`). */
export interface AppView {
  id: string;
  name: string;
  description: string;
  version: string;
  color: string;
  icon: string;
  repo: string;
  enabled: boolean;
  status: "off" | "on" | "installing" | "waiting" | "update" | "error";
  detail: string;
  permissions: string[];
  has_pc: boolean;
  elevated: boolean;
  settings: Setting[];
  values: Record<string, unknown>;
}

/** A registry app for Add app. */
export interface RegistryApp {
  id: string;
  name: string;
  description: string;
  version: string;
  color: string;
  icon: string;
  repo: string;
  pc?: { elevated: boolean } | null;
  permissions: string[];
  added: boolean;
}

export interface State {
  status: "searching" | "connected";
  board: Board | null;
  silent: Silent[];
  app_version: string;
  firmware_ok: boolean;
  apps: AppView[];
  bundled_firmware: string | null;
  update: { stage: "erasing" | "writing" | "restarting" | "done" | "failed"; percent: number; error: string } | null;
  /** The board streams its screen; started or stopped here or on the board (P14). */
  streaming: boolean;
}

export type Page = "apps" | "board" | "tasks" | "settings";

export interface LogLine {
  time: string;
  source: "pc" | "board" | "apps";
  problem: boolean;
  text: string;
}

// The window can open before the engine is managed (create order); retry the first read.
export async function getState(): Promise<State> {
  for (let i = 0; ; i++) {
    try {
      return await invoke<State>("get_state");
    } catch (e) {
      if (i >= 20) throw e;
      await new Promise((r) => setTimeout(r, 100));
    }
  }
}

export const onState = (cb: (s: State) => void): Promise<UnlistenFn> => listen<State>("state", (e) => cb(e.payload));
export const onLog = (cb: (l: LogLine) => void): Promise<UnlistenFn> => listen<LogLine>("log", (e) => cb(e.payload));
export const getLogs = () => invoke<LogLine[]>("get_logs");
export const openLogFolder = () => invoke("open_log_folder");
export const getAutostart = () => invoke<boolean>("get_autostart");
export const setAutostart = (on: boolean) => invoke<boolean>("set_autostart", { on });
export const rebootBoard = () => invoke("reboot_board");
export const mirrorSet = (on: boolean) => invoke("mirror_set", { on });
export const mirrorTouch = (x: number, y: number, down: boolean) => invoke("mirror_touch", { x, y, down });
/** Raw frame from the engine (see `mirror_frame` in main.rs). */
export const mirrorFrame = (since: number) => invoke<ArrayBuffer>("mirror_frame", { since });
/** One FreeRTOS task in a SYSINFO answer (proto/spec.md). */
export interface TaskInfo {
  id: number;
  /** 255 = either core. */
  core: number;
  priority: number;
  /** 0 running, 1 ready, 2 blocked, 3 suspended, 4 deleted. */
  state: number;
  /** µs on the `time_us` clock, wraps at 2^32. */
  runtime: number;
  stack_free: number;
  name: string;
}
/** The board's SYSINFO answer: raw counters that wrap at 2^32; rates come from two answers. */
export interface Sysinfo {
  time_us: number;
  cpu_mhz: number;
  heap_total: number;
  heap_free: number;
  heap_min: number;
  heap_largest: number;
  dma_free: number;
  flash_size: number;
  app_size: number;
  app_slot: number;
  apps_used: number;
  apps_total: number;
  nvs_used: number;
  nvs_total: number;
  rx_bytes: number;
  tx_bytes: number;
  rx_frames: number;
  rx_errors: number;
  fps_x10: number;
  backlight: number;
  /** 1 dimmed, 2 panel resting, 4 burn-in protection, 8 streaming, 16 Wi-Fi, 32 Bluetooth. */
  flags: number;
  tasks: TaskInfo[];
}
/** Asks the board for a new SYSINFO and returns the latest (Task manager, every second). */
export const getSysinfo = () => invoke<{ sample: Sysinfo | null; unsupported: boolean }>("sysinfo");
export const updateBoard = () => invoke("update_board");
export type Probe = { chip: string; flash: string; mac: string };
export const probeBoard = () => invoke<Probe>("probe_board");
export const installBoard = () => invoke("install_board");
export const finishWelcome = (openMain: boolean) => invoke("finish_welcome", { openMain });
export const showWelcome = () => invoke("show_welcome");
export const clearUpdate = () => invoke("clear_update");
export type PortItem = { name: string; product: string; board: boolean };
export const listPorts = () => invoke<{ selected: string | null; ports: PortItem[] }>("list_ports");
export const setPort = (port: string | null) => invoke("set_port", { port });
export type AppUpdate = {
  stage: "idle" | "checking" | "current" | "available" | "installing" | "failed";
  version: string;
  percent: number;
  error: string;
};
export const getAppUpdate = () => invoke<AppUpdate>("get_app_update");
export const checkAppUpdate = () => invoke<AppUpdate>("check_app_update");
export const installAppUpdate = () => invoke("install_app_update");
export const onAppUpdate = (cb: (u: AppUpdate) => void): Promise<UnlistenFn> =>
  listen<AppUpdate>("app_update", (e) => cb(e.payload));
export const listRegistry = () => invoke<RegistryApp[]>("list_registry");
export const addApp = (id: string) => invoke("add_app", { id });
export const setAppEnabled = (id: string, on: boolean) => invoke("set_app_enabled", { id, on });
export const setAppSetting = (id: string, key: string, value: unknown) => invoke("set_app_setting", { id, key, value });
export const appButton = (id: string, key: string) => invoke("app_button", { id, key });
export const removeApp = (id: string) => invoke("remove_app", { id });

/** Tile colour: a tile token name or `#RRGGBB` (docs/registry.md). */
export function tileColor(c: string): string {
  return c.startsWith("#") ? c : `var(--tile-${c})`;
}

/** Shown as: what an app's PC part does (docs/registry.md Permissions). */
export const permissionText: Record<string, { text: string; icon: string }> = {
  keys: { text: "Press keys and shortcuts", icon: "keyboard" },
  media: { text: "Change volume and control media", icon: "speaker-high" },
  "system-stats": { text: "Read CPU, memory and other system stats", icon: "cpu" },
  network: { text: "Use the network", icon: "globe" },
  files: { text: "Read and write files", icon: "folder" },
  programs: { text: "Start other programs", icon: "app-window" },
  helper: { text: "Runs its own helper program while switched on", icon: "puzzle-piece" },
  elevated: { text: "Asks for administrator rights each time it starts", icon: "shield-warning" },
};

/** Permission rows for an app: declared ones, then the two that come from the manifest itself. */
export function permissionRows(perms: string[], hasPc: boolean, elevated: boolean) {
  const keys = [...perms];
  if (hasPc) keys.push("helper");
  if (elevated) keys.push("elevated");
  return keys.filter((k) => permissionText[k]).map((k) => ({ key: k, ...permissionText[k] }));
}

export function uptime(s: number): string {
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  if (h) return `${h} h ${m} min`;
  if (m) return `${m} min`;
  return `${s} s`;
}
