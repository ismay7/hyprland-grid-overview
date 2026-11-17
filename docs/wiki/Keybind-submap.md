Defining a Hyprland submap named exactly `gridoverview` replaces the built-in keyboard navigation while the overview is open and gives you full control over its keybinds. The submap activates automatically when the overview opens; closing the overview restores the previous keymap.

You do **not** need a submap for normal use. Without one, arrows, Enter and Escape work out of the box. The starter module does not define a submap.

While the submap is active, regular Hyprland keybinds defined outside it are not handled by default. Add `{ submap_universal = true }` to every standard bind that should remain available while the overview is open (for example your usual workspace keys).

Mouse wheel actions can also be bound inside the submap. If a scroll action is not bound, gridoverview keeps its built-in wheel behaviour (grid axes; `SHIFT` / horizontal wheel for the other axis). Defining a matching scroll bind overrides that for as long as the submap is active.

Rename fields in the overview (scratchpad pencil, context pencil) keep working with a submap.

```lua
-- hyprland.lua
hl.define_submap("gridoverview", function()
    hl.bind("left",   hl.plugin.gridoverview.navigate("left"))
    hl.bind("right",  hl.plugin.gridoverview.navigate("right"))
    hl.bind("up",     hl.plugin.gridoverview.navigate("up"))
    hl.bind("down",   hl.plugin.gridoverview.navigate("down"))
    hl.bind("return", hl.plugin.gridoverview.overview("select"))
    hl.bind("escape", hl.plugin.gridoverview.overview("off"))
    hl.bind("mouse:272", function()
        -- Select the clicked window, or just the workspace if none was clicked, then close.
        hl.plugin.gridoverview.overview("select")
        hl.plugin.gridoverview.window("select")
        hl.plugin.gridoverview.overview("off")
    end, { mouse = true })
    hl.bind("mouse:274", hl.plugin.gridoverview.window("close"), { mouse = true })
end)

-- Example Hyprland bind that keeps working inside the submap:
for i = 1, 9 do
    hl.bind("SUPER + " .. i, hl.dsp.focus({ workspace = i }), { submap_universal = true })
end
```

[Back to Configuration](Configuration.md)
