// System screens: boot, Home (Tiles / Dock), Settings tree, Session sheet, Stream.
#pragma once
#include "fenix_sys.h"

namespace fx::apps {

void start();  // boot screen; Home follows when the sweep line has played
void on_link(const sys::LinkEvent& e);
void tick();  // call every UI loop pass (cheap when nothing changed)

}  // namespace fx::apps
