`hl.plugin.gridoverview.configure(options)` accepts a table of the gridoverview properties listed in [Basic configuration](Basic-configuration.md). Without `output`, it is shorthand for `hl.config({ plugin = { gridoverview = options } })`.

With `output`, it stores overrides for that monitor — useful when outputs need different scale, gaps, or tray width:

```lua
hl.plugin.gridoverview.configure({
    grid = true,
    scale = 0.5,
    workspace_gap = 40,
    shadow = { enabled = true, range = 50 },
})

hl.plugin.gridoverview.configure({
    output = "DP-1",
    scale = 0.45,
    special_tray_width = 260,
})

hl.plugin.gridoverview.configure({
    output = "HDMI-A-2",
    scale = 0.4,
    special_tray = false,
})

hl.bind("SUPER + D", function()
    hl.plugin.gridoverview.overview("toggle all")
end)
```

- `output` must match the exact connector name from `hyprctl monitors` (e.g. `DP-1`). No `all`, wildcards, or description selectors. A monitor may be disconnected when configured; overrides apply when it connects.
- Every overview property can be overridden, including individual `input` and `shadow` fields. Hyprland options outside gridoverview remain global.
- Missing fields inherit the current global value.
- Repeated calls for the same output merge fields — last value wins. An empty subtable leaves previous overrides unchanged.
- On config reload, monitor overrides are cleared and rebuilt from the config.
- `hl.config({ plugin = { gridoverview = ... } })` alone does not create a monitor rule; use `configure({ output = ... })` for that.

For which *workspaces* live on which monitor, see [Multi-monitor](Multi-monitor.md).
