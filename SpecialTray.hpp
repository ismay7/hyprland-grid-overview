#pragma once

extern "C" {
#include <lua.h>
}

class CScrollOverview;

// dismiss the centred rename field (overview close / Escape)
void cancelSpecialTrayRenameIfAny(CScrollOverview* overview = nullptr);

// hl.plugin.gridoverview.tray_layout(): JSON with the scratchpad tray of the overview under the pointer, in global
// logical px ({"open": false} without one); for scripts and scripts/selftest.py
int trayLayoutLua(lua_State* L);
