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
  uptime_s: number;
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

export interface State {
  status: "searching" | "connected";
  board: Board | null;
  silent: Silent[];
  app_version: string;
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

export function uptime(s: number): string {
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  if (h) return `${h} h ${m} min`;
  if (m) return `${m} min`;
  return `${s} s`;
}
