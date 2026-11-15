#pragma once

extern "C" {
#include <lua.h>
}

class CScrollOverview;

// kill a running rename prompt (tofi etc.) so it cannot keep the keyboard grab after the overview closes
void cancelSpecialTrayRenameIfAny(CScrollOverview* overview = nullptr);

// hl.plugin.gridoverview.tray_layout(): JSON with the scratchpad tray of the overview under the pointer, in global
// logical px ({"open": false} without one); for scripts and scripts/selftest.py
int trayLayoutLua(lua_State* L);
