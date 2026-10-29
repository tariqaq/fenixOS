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
  update: { stage: "writing" | "restarting" | "done" | "failed"; percent: number; error: string } | null;
}

export type Page = "apps" | "board" | "settings";

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
export const mirrorStop = () => invoke("mirror_stop");
export const mirrorTouch = (x: number, y: number, down: boolean) => invoke("mirror_touch", { x, y, down });
/** Raw frame from the engine (see `mirror_frame` in main.rs). */
export const mirrorFrame = (since: number) => invoke<ArrayBuffer>("mirror_frame", { since });
export const updateBoard = () => invoke("update_board");
export const clearUpdate = () => invoke("clear_update");
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
