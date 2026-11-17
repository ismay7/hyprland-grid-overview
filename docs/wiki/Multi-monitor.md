Workspace ids are global in Hyprland, so the grid is shared: every monitor's overview shows the contexts that live on it.

A context belongs to the monitor a workspace rule binds its first workspace (`R1`) to. Without a rule it goes to the monitor that already has workspaces of that context, and a context without any workspace goes to the monitor of workspace 11. Placeholders are only created for the overview's own contexts.

With the starter module, pin a context with `monitor = "HDMI-A-1"` in its `contexts` entry. Switching to that context then moves focus to that monitor, and new workspaces of the context are created there.

A separate grid per monitor is not supported yet. For different *look* settings per output (scale, gaps, tray width, …), see [Per-monitor configuration](Per-monitor-configuration.md).
