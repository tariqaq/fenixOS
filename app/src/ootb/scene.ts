// First-run 3D scene (PLAN §3.9): a procedural ESP32-2432S028R at real size (mm), a USB-C
// cable and a soft studio light on a black stage. Procedural, so no third-party asset licences.
//
// Model frame: X right, Y up, Z toward the viewer; the screen side faces +Z and the PCB's
// front face is z = 0. Part positions come from the board photos (pics/ANALYSIS.md), measured
// on the back as (u, v) mm from the back's top-left corner: seen from the front, x = 43 - u and
// y = 25 - v, so the USB ports sit on the left edge.
import * as THREE from "three";
import { getCurrentWindow } from "@tauri-apps/api/window";
import { RoomEnvironment } from "three/examples/jsm/environments/RoomEnvironment.js";
import { RoundedBoxGeometry } from "three/examples/jsm/geometries/RoundedBoxGeometry.js";
import { Screen, type ScreenMode } from "./screen";

const PCB_W = 86;
const PCB_H = 50;
const PCB_T = 1.6;
const HOLE = { x: 39.6, y: 21.6, r: 1.6 };
const USBC = { x: -40.35, y: 4, z: -PCB_T - 1.6 };
/** Plug tip x when fully in, and how far out it hovers while waiting. */
const TIP_IN = -37.2;
const HOVER = 34;
/** The ESP32 module's PCB antenna, past the right edge seen from the front (u = -1, v = 26). */
const ANTENNA = { x: 43.5, y: -1, z: -PCB_T - 0.4 };
/** Wi-Fi arcs: how many, one pass (ms), how far they grow (mm), and their spread (rad). */
const RINGS = 3;
const RING_MS = 1600;
const RING_R = 22;
const ARC = 1.8;

// DESIGN.md motion curve: cubic-bezier(0.2, 0.8, 0.2, 1), solved for x.
function bezier(x1: number, y1: number, x2: number, y2: number) {
  const cx = 3 * x1, bx = 3 * (x2 - x1) - cx, ax = 1 - cx - bx;
  const cy = 3 * y1, by = 3 * (y2 - y1) - cy, ay = 1 - cy - by;
  const sx = (t: number) => ((ax * t + bx) * t + cx) * t;
  const sy = (t: number) => ((ay * t + by) * t + cy) * t;
  return (x: number) => {
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    let t = x;
    for (let i = 0; i < 8; i++) {
      const d = (3 * ax * t + 2 * bx) * t + cx;
      if (Math.abs(d) < 1e-6) break;
      t -= (sx(t) - x) / d;
    }
    return sy(Math.min(1, Math.max(0, t)));
  };
}
export const ease = bezier(0.2, 0.8, 0.2, 1);

/** A value that eases from where it is to a target over a duration. */
class Tween {
  private from = 0;
  private to = 0;
  private start = 0;
  private dur = 1;
  constructor(v: number) {
    this.from = this.to = v;
  }
  set(to: number, now: number, dur: number) {
    if (to === this.to) return;
    this.from = this.get(now);
    this.to = to;
    this.start = now;
    this.dur = Math.max(1, dur);
  }
  get(now: number) {
    return this.from + (this.to - this.from) * ease((now - this.start) / this.dur);
  }
}

// Seeded random for the small parts, so every run draws the same board.
function rng(seed: number) {
  return () => {
    seed = (seed * 1664525 + 1013904223) >>> 0;
    return seed / 4294967296;
  };
}

const mat = {
  pcb: new THREE.MeshPhysicalMaterial({ color: 0x17171a, roughness: 0.55, clearcoat: 0.5, clearcoatRoughness: 0.35 }),
  gold: new THREE.MeshStandardMaterial({ color: 0xd4af6a, metalness: 1, roughness: 0.32 }),
  silver: new THREE.MeshStandardMaterial({ color: 0xc9cace, metalness: 1, roughness: 0.28 }),
  chip: new THREE.MeshStandardMaterial({ color: 0x111113, roughness: 0.6 }),
  module: new THREE.MeshStandardMaterial({ color: 0x1b1d22, roughness: 0.5 }),
  white: new THREE.MeshStandardMaterial({ color: 0xe6e2d8, roughness: 0.7 }),
  frame: new THREE.MeshStandardMaterial({ color: 0xd2d2d6, roughness: 0.45, metalness: 0.15 }),
  panel: new THREE.MeshPhysicalMaterial({ color: 0x030304, roughness: 0.12, clearcoat: 1 }),
  glass: new THREE.MeshPhysicalMaterial({
    color: 0x000000,
    roughness: 0.04,
    clearcoat: 1,
    transparent: true,
    opacity: 0.22,
    envMapIntensity: 1.4,
  }),
  rubber: new THREE.MeshPhysicalMaterial({ color: 0x232326, roughness: 0.62, clearcoat: 0.15 }),
  cable: new THREE.MeshPhysicalMaterial({ color: 0x1d1d20, roughness: 0.5, clearcoat: 0.2 }),
};
/** DESIGN.md `accent`, the same blue as the USB port glow. */
const ACCENT = 0x0a84ff;

function box(w: number, h: number, d: number, m: THREE.Material, x: number, y: number, z: number) {
  const b = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), m);
  b.position.set(x, y, z);
  return b;
}

/** A part on the back: (u, v) from the back's top-left corner, w along u, h along v. */
function back(w: number, h: number, d: number, m: THREE.Material, u: number, v: number, lift = 0) {
  return box(w, h, d, m, 43 - u, 25 - v, -PCB_T - lift - d / 2);
}

function pcb(): THREE.Mesh {
  const s = new THREE.Shape();
  const r = 2.5;
  const hw = PCB_W / 2;
  const hh = PCB_H / 2;
  s.moveTo(-hw + r, -hh);
  s.lineTo(hw - r, -hh);
  s.quadraticCurveTo(hw, -hh, hw, -hh + r);
  s.lineTo(hw, hh - r);
  s.quadraticCurveTo(hw, hh, hw - r, hh);
  s.lineTo(-hw + r, hh);
  s.quadraticCurveTo(-hw, hh, -hw, hh - r);
  s.lineTo(-hw, -hh + r);
  s.quadraticCurveTo(-hw, -hh, -hw + r, -hh);
  for (const [sx, sy] of [[1, 1], [1, -1], [-1, 1], [-1, -1]]) {
    const h = new THREE.Path();
    h.absarc(sx * HOLE.x, sy * HOLE.y, HOLE.r, 0, Math.PI * 2, true);
    s.holes.push(h);
  }
  const g = new THREE.ExtrudeGeometry(s, { depth: PCB_T, bevelEnabled: false, curveSegments: 24 });
  g.translate(0, 0, -PCB_T);
  return new THREE.Mesh(g, mat.pcb);
}

/** Traces, pads and silkscreen on the back, as a texture on a plane just off the PCB. */
function backArt(): THREE.Mesh {
  const k = 20; // px per mm
  const c = document.createElement("canvas");
  c.width = PCB_W * k;
  c.height = PCB_H * k;
  const g = c.getContext("2d")!;
  const rnd = rng(7);
  g.lineCap = "round";
  g.lineJoin = "round";
  g.strokeStyle = "rgba(58, 58, 64, 0.95)";
  g.lineWidth = 0.32 * k;
  for (let i = 0; i < 70; i++) {
    let x = rnd() * PCB_W;
    let y = rnd() * PCB_H;
    g.beginPath();
    g.moveTo(x * k, y * k);
    for (let j = 0; j < 3; j++) {
      const len = 3 + rnd() * 14;
      const dir = Math.floor(rnd() * 4);
      const diag = rnd() < 0.35;
      const dx = [1, 0, -1, 0][dir] * len;
      const dy = [0, 1, 0, -1][dir] * len;
      if (diag) {
        x += dx * 0.3;
        y += dx !== 0 ? Math.abs(dx) * 0.3 : dy * 0.3;
        g.lineTo(x * k, y * k);
      }
      x = Math.min(PCB_W - 1, Math.max(1, x + dx));
      y = Math.min(PCB_H - 1, Math.max(1, y + dy));
      g.lineTo(x * k, y * k);
    }
    g.stroke();
    g.fillStyle = "rgba(212, 175, 106, 0.85)";
    g.beginPath();
    g.arc(x * k, y * k, 0.45 * k, 0, Math.PI * 2);
    g.fill();
  }
  g.fillStyle = "rgba(236, 236, 240, 0.78)";
  g.textAlign = "center";
  g.font = `600 ${2.6 * k}px "Inter Variable", Inter, sans-serif`;
  g.fillText("ESP32-2432S028", 43 * k, 42 * k);
  g.font = `500 ${1.5 * k}px "Inter Variable", Inter, sans-serif`;
  for (const [t, u, v] of [
    ["TF V1.4", 53, 18.5],
    ["USB1", 77, 6],
    ["USB2", 76.5, 26.5],
    ["P1", 76, 36],
    ["RST", 7, 49],
    ["BOOT", 12.5, 49],
    ["SPEAK", 52, 44],
    ["P3", 10, 7.5],
    ["CN1", 27, 7.5],
  ] as [string, number, number][]) {
    g.fillText(t, u * k, v * k);
  }
  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 8;
  const m = new THREE.Mesh(
    new THREE.PlaneGeometry(PCB_W, PCB_H),
    new THREE.MeshStandardMaterial({ map: tex, transparent: true, roughness: 0.5, depthWrite: false }),
  );
  m.rotation.y = Math.PI; // faces -Z, so texture left = the back's left (+X)
  m.position.z = -PCB_T - 0.02;
  return m;
}

function board(screenTex: THREE.Texture): THREE.Group {
  const b = new THREE.Group();
  b.add(pcb(), backArt());
  // Gold rings round the four mounting holes, both faces.
  const ring = new THREE.RingGeometry(HOLE.r, HOLE.r + 1.3, 32);
  for (const [sx, sy] of [[1, 1], [1, -1], [-1, 1], [-1, -1]]) {
    for (const z of [0.01, -PCB_T - 0.01]) {
      const m = new THREE.Mesh(ring, mat.gold);
      m.position.set(sx * HOLE.x, sy * HOLE.y, z);
      if (z < 0) m.rotation.y = Math.PI;
      b.add(m);
    }
  }
  // Front: the 2.8" TFT module (69.2 × 50 mm, 57.6 × 43.2 mm active), white backlight frame,
  // black glass panel, the lit area, and a thin reflective cover.
  b.add(box(69.2, 50, 2.2, mat.frame, 0, 0, 1.1));
  b.add(box(68.6, 49.4, 0.8, mat.panel, 0, 0, 2.6));
  const screen = new THREE.Mesh(
    new THREE.PlaneGeometry(57.6, 43.2),
    new THREE.MeshBasicMaterial({ map: screenTex, toneMapped: false }),
  );
  screen.position.set(-2.2, 0, 3.01);
  b.add(screen);
  const cover = new THREE.Mesh(new THREE.PlaneGeometry(68.6, 49.4), mat.glass);
  cover.position.z = 3.04;
  b.add(cover);
  // Front ends: FPC tail and a few parts beside the panel.
  b.add(box(2.4, 18, 0.15, new THREE.MeshStandardMaterial({ color: 0xb06a1c, roughness: 0.5 }), 36.0, 0, 0.08));
  b.add(box(1.6, 0.8, 0.5, mat.chip, -38, -8, 0.25), box(1.6, 0.8, 0.5, mat.chip, -38, 9, 0.25));

  // Back: ESP32-WROOM-32 (25.5 × 18 mm) with its antenna over the edge and a metal shield.
  b.add(back(25.5, 18, 0.8, mat.module, 11.5, 26));
  b.add(back(17.6, 16, 2.4, mat.silver, 15.6, 26, 0.8));
  for (let i = 0; i < 4; i++) b.add(back(0.5, 12, 0.05, mat.gold, 1.2 + i * 1.4, 26, 0.8));
  b.add(back(14, 15, 1.9, mat.silver, 53, 8)); // microSD slot
  b.add(back(4, 10, 1.5, mat.chip, 64, 9)); // CH340C (SOP-16)
  b.add(back(6, 10, 0.25, mat.silver, 64, 9));
  b.add(back(5.5, 7.5, 2.6, mat.silver, 83.6, 10)); // micro-USB
  b.add(back(7.3, 8.9, 3.2, mat.silver, 83.6, 21)); // USB-C
  b.add(back(1.2, 6.8, 2.2, mat.chip, 86.6, 21, 0.5)); // USB-C mouth
  b.add(back(3.5, 6, 3, mat.white, 81, 33)); // P1 (4P 1.25)
  b.add(back(6, 3.5, 3, mat.white, 10, 3), back(6, 3.5, 3, mat.white, 27, 3)); // P3, CN1
  b.add(back(4, 3.5, 3, mat.white, 52, 47)); // speaker
  b.add(back(5, 4.4, 1.1, mat.chip, 31, 43)); // XPT2046
  b.add(back(6.5, 3.5, 1.6, mat.chip, 64, 30)); // AMS1117
  b.add(back(3.5, 3.5, 1.2, mat.white, 37, 10)); // RGB LED
  for (const u of [7, 12.5]) {
    b.add(back(3.5, 6, 2, mat.white, u, 45.5)); // RST, BOOT
    b.add(back(1.6, 2.4, 0.8, mat.chip, u, 45.5, 2));
  }
  const rnd = rng(42);
  const free = [
    [27, 2, 44, 18], [42, 22, 74, 38], [57, 34, 76, 46], [20, 36, 46, 48], [67, 3, 76, 18],
  ];
  for (let i = 0; i < 46; i++) {
    const [u0, v0, u1, v1] = free[i % free.length];
    const big = rnd() < 0.3;
    const rot = rnd() < 0.5;
    const [w, h] = big ? [2, 1.25] : [1.6, 0.8];
    b.add(back(rot ? h : w, rot ? w : h, big ? 0.8 : 0.5, rnd() < 0.6 ? mat.chip : mat.white, u0 + rnd() * (u1 - u0), v0 + rnd() * (v1 - v0)));
  }
  return b;
}

/** USB-C plug and cable. Local origin = plug tip, insertion along +X. */
function cable(): THREE.Group {
  const c = new THREE.Group();
  const shell = new THREE.Mesh(new RoundedBoxGeometry(6.6, 8.3, 2.5, 3, 1.1), mat.silver);
  shell.position.x = -3.3;
  const body = new THREE.Mesh(new RoundedBoxGeometry(18, 11.5, 6, 4, 2.6), mat.rubber);
  body.position.x = -6.6 - 9;
  const relief = new THREE.Mesh(new THREE.CylinderGeometry(2.4, 2.9, 8, 24), mat.rubber);
  relief.rotation.z = Math.PI / 2;
  relief.position.x = -6.6 - 18 - 4;
  const path = new THREE.CatmullRomCurve3([
    new THREE.Vector3(-32, 0, 0),
    new THREE.Vector3(-60, -2, -4),
    new THREE.Vector3(-95, -22, -26),
    new THREE.Vector3(-120, -70, -60),
    new THREE.Vector3(-130, -160, -90),
  ]);
  const wire = new THREE.Mesh(new THREE.TubeGeometry(path, 80, 2.1, 16, false), mat.cable);
  c.add(shell, body, relief, wire);
  return c;
}

export type Pose = "intro" | "waiting" | "plugged" | "wireless" | "front";

/** Render limits. Both windows use the GPU; 60 fps keeps a 165 Hz panel from costing a core. */
export interface SceneLimits {
  /** Frames per second at most (default 60). */
  maxFps?: number;
  /** Device pixel ratio at most (default 2). */
  maxDpr?: number;
}

export class BoardScene {
  private r: THREE.WebGLRenderer;
  private scene = new THREE.Scene();
  private cam = new THREE.PerspectiveCamera(28, 1, 1, 2000);
  private board: THREE.Group;
  private cable: THREE.Group;
  private glow: THREE.PointLight;
  /** Wi-Fi run: arcs that face the camera and grow out from the antenna, and its light. */
  private waves = new THREE.Group();
  private aglow: THREE.PointLight;
  private wavesT = 0;
  /** Linked over Wi-Fi: set by the `wireless` pose; `front` then keeps the cable away. */
  private air = false;
  private screen = new Screen();
  private tex: THREE.CanvasTexture;
  private raf = 0;
  private yaw = new Tween(-1.1);
  private pitch = new Tween(0.35);
  private dist = new Tween(260);
  /** Camera pan (mm): negative puts the board right of centre, clear of the copy at the left. */
  private pan = new Tween(0);
  private tip = new Tween(TIP_IN - HOVER - 60);
  private shown = new Tween(0);
  private glowT = 0;
  private pose: Pose = "intro";
  mode: ScreenMode = "off";
  progress = 0;
  /** prefers-reduced-motion: no drift, no bobbing, moves become cuts (the page cross-fades). */
  reduced: boolean;
  /** Frame rate over the last second, for the S8 numbers. */
  fps = 0;
  /** No frames while the window is minimized or the page hidden. */
  private minimized = false;
  private get paused() {
    return this.minimized || document.hidden;
  }
  private minGap: number;
  private unlisten?: () => void;
  private last = -Infinity;

  constructor(canvas: HTMLCanvasElement, reduced: boolean, limits: SceneLimits = {}) {
    this.reduced = reduced;
    // A little under the frame time, so a 30 fps cap on a 60 Hz display takes every other frame.
    this.minGap = 1000 / (limits.maxFps ?? 60) - 4;
    this.r = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: "high-performance" });
    this.r.setPixelRatio(Math.min(limits.maxDpr ?? 2, window.devicePixelRatio));
    this.r.toneMapping = THREE.ACESFilmicToneMapping;
    this.r.toneMappingExposure = 0.9;
    this.scene.background = new THREE.Color(0x000000);
    const pm = new THREE.PMREMGenerator(this.r);
    this.scene.environment = pm.fromScene(new RoomEnvironment(), 0.04).texture;
    this.scene.environmentIntensity = 0.8;
    pm.dispose();
    // Soft studio light: a warm key from the upper left, a cool rim from behind.
    const key = new THREE.DirectionalLight(0xfff4e6, 2.2);
    key.position.set(-120, 160, 200);
    const rim = new THREE.DirectionalLight(0x9ec5ff, 1.2);
    rim.position.set(140, 60, -180);
    this.scene.add(key, rim);

    this.tex = new THREE.CanvasTexture(this.screen.canvas);
    this.tex.colorSpace = THREE.SRGBColorSpace;
    this.tex.anisotropy = 8;
    this.board = board(this.tex);
    this.cable = cable();
    this.cable.position.set(0, USBC.y, USBC.z);
    this.board.add(this.cable);
    this.glow = new THREE.PointLight(ACCENT, 0, 40, 2);
    this.glow.position.set(-46, USBC.y, USBC.z);
    this.board.add(this.glow);
    // Arcs open away from the board (local +X faces screen right, where the antenna is).
    const ring = new THREE.RingGeometry(1, 1.035, 48, 1, -ARC / 2, ARC);
    for (let i = 0; i < RINGS; i++) {
      const m = new THREE.MeshBasicMaterial({
        color: ACCENT,
        transparent: true,
        opacity: 0,
        depthWrite: false,
        toneMapped: false,
        side: THREE.DoubleSide,
      });
      this.waves.add(new THREE.Mesh(ring, m));
    }
    this.waves.position.set(ANTENNA.x, ANTENNA.y, ANTENNA.z);
    this.waves.visible = false;
    this.aglow = new THREE.PointLight(ACCENT, 0, 50, 2);
    this.aglow.position.set(ANTENNA.x, ANTENNA.y, ANTENNA.z - 3);
    this.board.add(this.waves, this.aglow);
    this.scene.add(this.board);
    this.resize();
    this.raf = requestAnimationFrame(this.frame);
    // WebView2 keeps a minimized window "visible" and its frames coming: pause while minimized.
    const win = getCurrentWindow();
    const check = () => win.isMinimized().then((m) => (this.minimized = m), () => {});
    check();
    win.onResized(check).then((f) => (this.unlisten = f), () => {});
  }

  /** The version the boot screen shows. */
  set version(v: string) {
    this.screen.version = v;
  }

  resize() {
    const w = this.r.domElement.clientWidth;
    const h = this.r.domElement.clientHeight;
    this.r.setSize(w, h, false);
    this.cam.aspect = w / Math.max(1, h);
    this.cam.updateProjectionMatrix();
  }

  /** Move the board and cable to a pose (eased; a cut with reduced motion). */
  setPose(p: Pose) {
    const now = performance.now();
    const slow = this.reduced ? 1 : 2200;
    const mid = this.reduced ? 1 : 1400;
    this.pose = p;
    if (p !== "front") this.air = p === "wireless";
    this.shown.set(1, now, this.reduced ? 1 : 1800);
    // Over Wi-Fi the board sits nearer the centre, leaving room for the arcs at its right.
    this.pan.set(p === "intro" ? 0 : p === "wireless" ? -6 : -22, now, slow);
    switch (p) {
      case "intro":
        this.yaw.set(-0.35, now, slow);
        this.pitch.set(0.22, now, slow);
        this.dist.set(205, now, slow);
        this.tip.set(TIP_IN - HOVER - 60, now, 1);
        break;
      case "waiting":
        // Turn the ports' edge toward the viewer; the cable waits beside the USB-C port.
        this.yaw.set(0.62, now, mid);
        this.pitch.set(0.18, now, mid);
        this.dist.set(190, now, mid);
        this.tip.set(TIP_IN - HOVER, now, mid);
        break;
      case "plugged":
        this.yaw.set(0.62, now, mid);
        this.pitch.set(0.18, now, mid);
        this.dist.set(190, now, mid);
        this.tip.set(TIP_IN, now, this.reduced ? 1 : 700);
        this.glowT = now + (this.reduced ? 1 : 650);
        break;
      case "wireless":
        // No cable: it pulls away and the antenna edge turns toward the viewer.
        this.yaw.set(-0.5, now, mid);
        this.pitch.set(0.16, now, mid);
        this.dist.set(200, now, mid);
        this.tip.set(TIP_IN - HOVER - 60, now, this.reduced ? 1 : 900);
        this.wavesT = now + (this.reduced ? 1 : 500);
        break;
      case "front":
        this.yaw.set(0.16, now, slow);
        this.pitch.set(0.08, now, slow);
        this.dist.set(178, now, slow);
        if (!this.air) this.tip.set(TIP_IN, now, 1);
        break;
    }
  }

  private frames = 0;
  private fpsAt = 0;

  private frame = (now: number) => {
    this.raf = requestAnimationFrame(this.frame);
    if (this.paused || now - this.last < this.minGap) return;
    this.last = now;
    const drift = this.reduced ? 0 : 1;
    const yaw = this.yaw.get(now) + drift * 0.05 * Math.sin(now / 6100);
    const pitch = this.pitch.get(now) + drift * 0.025 * Math.sin(now / 8300 + 1);
    this.board.rotation.set(pitch, yaw, 0);
    this.board.position.y = drift * 0.8 * Math.sin(now / 4700);
    const shown = this.shown.get(now);
    this.board.scale.setScalar(0.94 + 0.06 * shown);
    this.r.toneMappingExposure = 0.9 * shown;
    let tip = this.tip.get(now);
    if (this.pose === "waiting" && !this.reduced) tip += 2.2 * Math.sin(now / 520);
    this.cable.position.x = tip;
    this.cable.visible = tip > TIP_IN - HOVER - 59;
    // The port glows as the plug seats, then settles to a faint light.
    const since = now - this.glowT;
    const plugged = this.pose === "plugged" || (this.pose === "front" && !this.air);
    this.glow.intensity = plugged && since > 0 ? 260 * Math.exp(-since / 900) + 25 : 0;
    // Wi-Fi: arcs leave the antenna one after another and fade as they grow (none with reduced
    // motion: the antenna light only).
    const air = now - this.wavesT;
    const wireless = this.air && (this.pose === "wireless" || this.pose === "front") && air > 0;
    this.aglow.intensity = wireless ? 160 * Math.exp(-air / 1200) + 30 : 0;
    this.waves.visible = wireless && !this.reduced;
    if (this.waves.visible) {
      // Face the camera: undo the board's turn.
      this.waves.quaternion.copy(this.board.quaternion).invert();
      this.waves.children.forEach((c, i) => {
        const t = (air / RING_MS - i / RINGS + 1) % 1;
        const born = air >= (i * RING_MS) / RINGS; // the first pass starts one ring at a time
        const m = c as THREE.Mesh<THREE.RingGeometry, THREE.MeshBasicMaterial>;
        m.scale.setScalar(4 + RING_R * ease(t));
        m.material.opacity = born ? 0.85 * (1 - t) ** 1.6 : 0;
      });
    }
    const d = this.dist.get(now);
    const pan = this.pan.get(now);
    this.cam.position.set(pan, 0, d);
    this.cam.lookAt(pan, 0, 0);
    try {
      this.screen.draw(this.mode, now, this.progress);
    } catch (e) {
      console.error("screen", e); // keep the scene running whatever the 2D canvas does
    }
    this.tex.needsUpdate = true;
    this.r.render(this.scene, this.cam);
    this.frames++;
    if (now - this.fpsAt >= 1000) {
      this.fps = (this.frames * 1000) / (now - this.fpsAt);
      this.frames = 0;
      this.fpsAt = now;
    }
  };

  dispose() {
    this.unlisten?.();
    cancelAnimationFrame(this.raf);
    this.r.dispose();
  }
}
