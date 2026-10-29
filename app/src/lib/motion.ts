// PC motion, per DESIGN.md (Motion): one ease-out curve, 200 ms (press, toggle, menus),
// 250 ms (fades, page changes, nav push) and 350 ms (sheets), a light spring for tiles. With
// reduced motion every transition here is a plain fade.
import { backOut } from "svelte/easing";
import type { TransitionConfig } from "svelte/transition";

export const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
export const FAST = 200;
export const BASE = 250;
export const SLOW = 350;

// cubic-bezier(0.2, 0.8, 0.2, 1) (`--ease` in tokens.css) as an easing function.
function bezier(x1: number, y1: number, x2: number, y2: number) {
  const a = (p1: number, p2: number) => 1 - 3 * p2 + 3 * p1;
  const b = (p1: number, p2: number) => 3 * p2 - 6 * p1;
  const c = (p1: number) => 3 * p1;
  const at = (t: number, p1: number, p2: number) => ((a(p1, p2) * t + b(p1, p2)) * t + c(p1)) * t;
  const slope = (t: number, p1: number, p2: number) => 3 * a(p1, p2) * t * t + 2 * b(p1, p2) * t + c(p1);
  return (x: number) => {
    let t = x;
    for (let i = 0; i < 8; i++) {
      const d = slope(t, x1, x2);
      if (Math.abs(d) < 1e-6) break;
      t -= (at(t, x1, x2) - x) / d;
    }
    return at(Math.min(1, Math.max(0, t)), y1, y2);
  };
}
export const ease = bezier(0.2, 0.8, 0.2, 1);

const fadeOnly = (duration: number, delay = 0): TransitionConfig => ({ duration, delay, css: (t) => `opacity: ${t}` });

/** Page change and nav push: fade in while moving `x` / `y` px into place. */
export function enter(_: Element, { x = 0, y = 8, duration = BASE, delay = 0 } = {}): TransitionConfig {
  if (reduced) return fadeOnly(duration, delay);
  return {
    duration,
    delay,
    easing: ease,
    css: (t, u) => `opacity: ${t}; transform: translate(${u * x}px, ${u * y}px)`,
  };
}

/** Sheets and dialogs: rise 16 px and grow from 98 %. Use `in:sheet` and `out:sheet={{ duration: FAST }}`. */
export function sheet(_: Element, { duration = SLOW } = {}): TransitionConfig {
  if (reduced) return fadeOnly(duration);
  return {
    duration,
    easing: ease,
    css: (t, u) => `opacity: ${t}; transform: translateY(${u * 16}px) scale(${1 - u * 0.02})`,
  };
}

/** The scrim behind a sheet. */
export function scrim(_: Element, { duration = SLOW } = {}): TransitionConfig {
  return { duration, easing: ease, css: (t) => `opacity: ${t}` };
}

/** Menus: grow from 96 % at their anchor corner. */
export function menu(_: Element, { duration = FAST } = {}): TransitionConfig {
  if (reduced) return fadeOnly(duration);
  return { duration, easing: ease, css: (t, u) => `opacity: ${t}; transform: scale(${1 - u * 0.04})` };
}

/** Tiles appearing: fade and scale 0.8 to 1 on a light spring (about 2 % overshoot). */
export function pop(_: Element, { duration = SLOW, delay = 0 } = {}): TransitionConfig {
  if (reduced) return fadeOnly(BASE, delay);
  return {
    duration,
    delay,
    css: (t) => `opacity: ${Math.min(1, t * 2)}; transform: scale(${0.8 + 0.2 * backOut(t)})`,
  };
}
