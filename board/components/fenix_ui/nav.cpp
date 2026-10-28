// Screen stack. Only builders are stored; a screen is rebuilt when it becomes visible again,
// so at most two screens exist (only during a slide).
#include "fenix_ui.h"

namespace fx::ui {

static constexpr int MAX_DEPTH = 8;
static Builder s_stack[MAX_DEPTH];
static int s_depth;

static void show(Builder b, lv_screen_load_anim_t anim) {
  sheet_close();
  lv_obj_t* scr = screen_new();
  b(scr);
  if (anim == LV_SCREEN_LOAD_ANIM_NONE) {
    lv_obj_t* old = lv_screen_active();
    lv_screen_load(scr);
    if (old && old != scr) lv_obj_delete(old);
  } else {
    lv_screen_load_anim(scr, anim, FX_MOTION_BASE_MS, 0, true);
  }
}

void nav_root(Builder b, bool animate) {
  s_depth = 1;
  s_stack[0] = b;
  show(b, animate ? LV_SCREEN_LOAD_ANIM_FADE_IN : LV_SCREEN_LOAD_ANIM_NONE);
}

void nav_push(Builder b) {
  if (s_depth >= MAX_DEPTH) return;
  s_stack[s_depth++] = b;
  show(b, LV_SCREEN_LOAD_ANIM_MOVE_LEFT);
}

void nav_pop() {
  if (s_depth <= 1) return;
  s_depth--;
  show(s_stack[s_depth - 1], LV_SCREEN_LOAD_ANIM_MOVE_RIGHT);
}

void nav_rebuild() {
  if (s_depth) show(s_stack[s_depth - 1], LV_SCREEN_LOAD_ANIM_NONE);
}

Builder nav_top() { return s_depth ? s_stack[s_depth - 1] : nullptr; }
int nav_depth() { return s_depth; }

}  // namespace fx::ui
