# Grid Overview

**See Hyprland, then shape it.** `gridoverview` (Hyprland **0.57.2+**) shows your workspaces, windows and scratchpads as one grid you can grab with the mouse — Hyprland core features (workspaces, scratchpads, groups, pins, float/fullscreen) via overview, mouse and modifiers. Nothing has to be configured first; refine later if you want.

Workspace ids follow `row * 10 + column` (11–19, 21–29, …). The grid grows as you need it: every row gets an empty workspace past its last used one, and below the last used row there is an empty row, so a window can always be dropped somewhere new. Tiled drop-into-split is built for **Dwindle** (other layouts untested for that).

- [Installation](Installation.md) — hyprpm + toggle bind (Lua); optional starter module
- [Configuration](Configuration.md) — options, dispatchers, gestures
- [Advanced configuration](Advanced-configuration.md) — contexts, scratchpad tray, multi-monitor
