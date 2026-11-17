## Using Hyprpm (recommended)

1. Add the plugin repository:

   ```bash
   hyprpm add https://github.com/ismay7/hyprland-grid-overview
   ```

2. Build and fetch dependencies:

   ```bash
   hyprpm update
   ```

3. Enable the plugin:

   ```bash
   hyprpm enable gridoverview
   ```

4. Load enabled plugins when Hyprland starts — Lua:

   ```lua
   hl.on("hyprland.start", function()
       hl.exec_cmd("hyprpm reload")
   end)
   ```

   or in a classic `.conf`:

   ```ini
   exec-once = hyprpm reload
   ```

5. **Add one keybind** so you can open the overview (the plugin registers no binds of its own). Use your main modifier — examples below use Super:

   Lua:

   ```lua
   hl.bind("SUPER + D", function()
       if hl.plugin and hl.plugin.gridoverview then
           hl.plugin.gridoverview.overview("toggle")
       end
   end)
   ```

   Hyprlang (`.conf`), if you use `$mainMod`:

   ```ini
   bind = $mainMod, D, gridoverview:overview, toggle
   ```

   Reload Hyprland (or the config). Press the bind — free grid, scratchpad tray, groups/pins chrome and zoom defaults are already on.

### Optional: starter module

For grid workspace keys (`R*10+C`), continuous zoom on the wheel, and opt-in contexts, copy [examples/gridoverview.lua](../../examples/gridoverview.lua) to `~/.config/hypr/gridoverview.lua` and `require("gridoverview")` from `hyprland.lua`. Set `settings.mainMod` if your main modifier is not Super. Contexts stay opt-in (`useContexts`, `context_labels`, …).

**Layout:** Grid Overview is designed for Hyprland's **Dwindle** layout. Edge-insert while dragging relies on Dwindle splits; other layouts are untested for that path.

## Building from source

1. Clone the repository and enter its directory:

   ```bash
   git clone https://github.com/ismay7/hyprland-grid-overview.git
   cd hyprland-grid-overview
   ```

2. Build the plugin (needs headers matching the running Hyprland):

   ```bash
   make all
   ```

3. Load the plugin into the running Hyprland instance:

   ```bash
   hyprctl plugin load "$(pwd)/gridoverview.so"
   ```

4. Prefer `hyprpm` for updates, or load from Lua with an absolute path:

   ```lua
   local pluginPath = "/absolute/path/to/hyprland-grid-overview/gridoverview.so"
   hl.plugin.load(pluginPath)
   ```

5. Add the same **one keybind** as in the hyprpm steps above.
