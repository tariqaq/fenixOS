// Screen stack. Only builders are stored; a screen is rebuilt when it becomes visible again,
// so at most two screens exist (only during a slide). A faded push (an app when the heap is short,
// app_pkg.cpp slide_fits, P57) and the pop back from it delete the old screen before the new one is
// built (P50-M): a big app and Home never need RAM at the same time; the new screen fades in from
// black.
#include "fenix_ui.h"

namespace fx::ui {

static constexpr int MAX_DEPTH = 8;
static Builder s_stack[MAX_DEPTH];
static bool s_faded[MAX_DEPTH];  // pushed with a fade: the pop back fades too
static int s_depth;

static void show(Builder b, lv_screen_load_anim_t anim, bool free_first = false) {
  sheet_close();
  // A blank screen in between: loading it with no animation deletes the old one at once (and
  // ends a slide still running); the new screen's fade then deletes the blank.
  if (free_first) lv_screen_load_anim(screen_new(), LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
  lv_obj_t* scr = screen_new();
  b(scr);
  // Lay out now: a press read before the next refresh would hit-test rows still at 0,0 with
  // no size and land on the scroll area, losing the tap (P37: a CLI tap right after the
  // connect rebuild).
  lv_obj_update_layout(scr);
  // Always through lv_screen_load_anim with auto-delete: it also finishes a slide still
  // running, so the old screen is never deleted twice or left half moved.
  uint32_t ms = anim == LV_SCREEN_LOAD_ANIM_NONE ? 0 : FX_MOTION_BASE_MS;
  lv_screen_load_anim(scr, anim, ms, 0, true);
}

void nav_root(Builder b, bool animate) {
  s_depth = 1;
  s_stack[0] = b;
  show(b, animate ? LV_SCREEN_LOAD_ANIM_FADE_IN : LV_SCREEN_LOAD_ANIM_NONE);
}

void nav_push(Builder b, bool fade) {
  if (s_depth >= MAX_DEPTH) return;
  s_faded[s_depth] = fade;
  s_stack[s_depth++] = b;
  show(b, fade ? LV_SCREEN_LOAD_ANIM_FADE_IN : LV_SCREEN_LOAD_ANIM_MOVE_LEFT, fade);
}

void nav_pop() {
  if (s_depth <= 1) return;
  bool fade = s_faded[--s_depth];
  show(s_stack[s_depth - 1], fade ? LV_SCREEN_LOAD_ANIM_FADE_IN : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, fade);
}

void nav_rebuild() {
  if (s_depth) show(s_stack[s_depth - 1], LV_SCREEN_LOAD_ANIM_NONE);
}

Builder nav_top() { return s_depth ? s_stack[s_depth - 1] : nullptr; }
int nav_depth() { return s_depth; }

}  // namespace fx::ui
