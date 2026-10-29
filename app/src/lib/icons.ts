// Phosphor icons used by the PC app (bundled from @phosphor-icons/core, no CDN). Names follow
// the artboards in design/mockups (fill weight unless the artboard draws bold or regular).
import squaresFour from "@phosphor-icons/core/assets/fill/squares-four-fill.svg?raw";
import cpu from "@phosphor-icons/core/assets/fill/cpu-fill.svg?raw";
import gearSix from "@phosphor-icons/core/assets/fill/gear-six-fill.svg?raw";
import usb from "@phosphor-icons/core/assets/bold/usb-bold.svg?raw";
import plus from "@phosphor-icons/core/assets/bold/plus-bold.svg?raw";
import puzzlePiece from "@phosphor-icons/core/assets/fill/puzzle-piece-fill.svg?raw";
import checkCircle from "@phosphor-icons/core/assets/fill/check-circle-fill.svg?raw";
import arrowClockwise from "@phosphor-icons/core/assets/regular/arrow-clockwise.svg?raw";
import downloadSimple from "@phosphor-icons/core/assets/bold/download-simple-bold.svg?raw";
import x from "@phosphor-icons/core/assets/bold/x-bold.svg?raw";
import caretUpDown from "@phosphor-icons/core/assets/bold/caret-up-down-bold.svg?raw";
import warning from "@phosphor-icons/core/assets/fill/warning-fill.svg?raw";
import listBullets from "@phosphor-icons/core/assets/regular/list-bullets.svg?raw";
import caretLeft from "@phosphor-icons/core/assets/bold/caret-left-bold.svg?raw";
import magnifyingGlass from "@phosphor-icons/core/assets/bold/magnifying-glass-bold.svg?raw";
import check from "@phosphor-icons/core/assets/bold/check-bold.svg?raw";
import pulse from "@phosphor-icons/core/assets/fill/pulse-fill.svg?raw";
import wifiSlash from "@phosphor-icons/core/assets/fill/wifi-slash-fill.svg?raw";
import bluetoothSlash from "@phosphor-icons/core/assets/fill/bluetooth-slash-fill.svg?raw";

export const icons = {
  "squares-four": squaresFour,
  cpu,
  "gear-six": gearSix,
  usb,
  plus,
  "puzzle-piece": puzzlePiece,
  "check-circle": checkCircle,
  "arrow-clockwise": arrowClockwise,
  "download-simple": downloadSimple,
  x,
  "caret-up-down": caretUpDown,
  warning,
  "list-bullets": listBullets,
  "caret-left": caretLeft,
  "magnifying-glass": magnifyingGlass,
  check,
  pulse,
  "wifi-slash": wifiSlash,
  "bluetooth-slash": bluetoothSlash,
} as const;

export type IconName = keyof typeof icons;
