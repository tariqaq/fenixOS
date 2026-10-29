// The board's screen in the 3D model: a 640 × 480 canvas (2× the real 320 × 240), drawn to
// match the firmware: the boot screen (board/components/fenix_apps/shell.cpp), an install
// progress line on the same track, and the Home screen (docs/screens/board/home.png).
import gearSvg from "@phosphor-icons/core/assets/fill/gear-fill.svg?raw";
import powerSvg from "@phosphor-icons/core/assets/fill/power-fill.svg?raw";
import castSvg from "@phosphor-icons/core/assets/fill/screencast-fill.svg?raw";
import plugSvg from "@phosphor-icons/core/assets/fill/plugs-connected-fill.svg?raw";

export type ScreenMode = "off" | "boot" | "install" | "home";

const W = 640;
const H = 480;
const FONT = '"Inter Variable", Inter, system-ui, sans-serif';
const LABEL_2 = "#98989F";
const SURFACE_3 = "#3A3A3C";

function icon(svg: string, color: string): HTMLImageElement {
  const img = new Image();
  // Phosphor files already set fill="currentColor": swap the colour in, do not add a second fill. A
  // size is set so the image has a natural size.
  img.src = "data:image/svg+xml;utf8," + encodeURIComponent(svg.replaceAll("currentColor", color).replace("<svg ", '<svg width="256" height="256" '));
  return img;
}

const icons = {
  gear: icon(gearSvg, "#FFFFFF"),
  power: icon(powerSvg, "#FFFFFF"),
  cast: icon(castSvg, "#FFFFFF"),
  plug: icon(plugSvg, "#30D158"),
};

// A broken image is also `complete`, and drawImage throws on it.
const ready = (img: HTMLImageElement) => img.complete && img.naturalWidth > 0;

// lv_anim_path_ease_in_out: a cubic in-out.
const inOut = (t: number) => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2);

export class Screen {
  readonly canvas = document.createElement("canvas");
  private g: CanvasRenderingContext2D;
  version = "";

  constructor() {
    this.canvas.width = W;
    this.canvas.height = H;
    this.g = this.canvas.getContext("2d")!;
  }

  /** Draw `mode` at time `t` (ms). `progress` is 0..1 for "install". */
  draw(mode: ScreenMode, t: number, progress = 0) {
    const g = this.g;
    g.fillStyle = "#000";
    g.fillRect(0, 0, W, H);
    if (mode === "off") return;
    if (mode === "home") return this.home();
    // Boot / install: wordmark, version, a 120 × 3 track (all ×2), centred as a column.
    const top = 170;
    g.textAlign = "center";
    g.textBaseline = "alphabetic";
    g.fillStyle = "#FFF";
    g.font = `600 44px ${FONT}`;
    g.fillText("fenixOS", W / 2, top + 40);
    g.fillStyle = LABEL_2;
    g.font = `400 26px ${FONT}`;
    g.fillText(mode === "install" ? `Installing ${Math.round(progress * 100)}%` : `v${this.version}`, W / 2, top + 82);
    const tx = W / 2 - 120;
    const ty = top + 118;
    g.save();
    roundRect(g, tx, ty, 240, 6, 3);
    g.fillStyle = SURFACE_3;
    g.fill();
    g.clip();
    g.fillStyle = "#FFF";
    if (mode === "install") {
      g.fillRect(tx, ty, 240 * Math.min(1, Math.max(0, progress)), 6);
    } else {
      // 900 ms sweep + 100 ms pause, from -36 to 120 px (×2).
      const k = (t % 1000) / 900;
      if (k <= 1) {
        const x = -72 + (240 + 72) * inOut(k);
        roundRect(g, tx + x, ty, 72, 6, 3);
        g.fill();
      }
    }
    g.restore();
  }

  private home() {
    const g = this.g;
    const now = new Date();
    const hh = String(now.getHours()).padStart(2, "0");
    const mm = String(now.getMinutes()).padStart(2, "0");
    g.textAlign = "left";
    g.textBaseline = "alphabetic";
    g.fillStyle = "#FFF";
    g.font = `600 128px ${FONT}`;
    g.fillText(`${hh}:${mm}`, 20, 156);
    g.fillStyle = LABEL_2;
    g.font = `400 26px ${FONT}`;
    const date = now.toLocaleDateString("en-GB", { weekday: "long", day: "numeric", month: "long" });
    g.fillText(date.replace(/^(\w+) /, "$1, "), 26, 202);
    if (ready(icons.plug)) g.drawImage(icons.plug, 548, 44, 30, 30);
    g.fillStyle = LABEL_2;
    g.font = `400 24px ${FONT}`;
    g.fillText("PC", 586, 68);
    const tiles: [string, string, HTMLImageElement][] = [
      ["Settings", "#636366", icons.gear],
      ["Session", "#4B48C9", icons.power],
      ["Stream", "#0062CC", icons.cast],
    ];
    tiles.forEach(([name, color, img], i) => {
      const x = 24 + i * 200;
      const y = 328;
      roundRect(g, x, y, 184, 132, 28);
      g.fillStyle = color;
      g.fill();
      if (ready(img)) g.drawImage(img, x + 22, y + 22, 40, 40);
      g.fillStyle = "#FFF";
      g.font = `600 28px ${FONT}`;
      g.fillText(name, x + 22, y + 108);
    });
  }
}

function roundRect(g: CanvasRenderingContext2D, x: number, y: number, w: number, h: number, r: number) {
  g.beginPath();
  g.roundRect(x, y, w, h, r);
}
