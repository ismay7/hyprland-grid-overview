Requires **Hyprland 0.57.2** or newer. Config is **Lua only** (`hyprland.lua`).

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

4. Load enabled plugins when Hyprland starts (`hyprland.lua`):

   ```lua
   hl.on("hyprland.start", function()
       hl.exec_cmd("hyprpm reload")
   end)
   ```

5. **Bind a toggle — required.** The plugin registers no keybinds. Until you do this, there is no way to open the overview from the keyboard:

   ```lua
   -- use your main modifier (SUPER, ALT, …)
   hl.bind("SUPER + D", function()
       if hl.plugin and hl.plugin.gridoverview then
           hl.plugin.gridoverview.overview("toggle")
       end
   end)
   ```

   Reload the config, press the bind — free grid, scratchpad tray, groups/pins chrome and zoom defaults are already on.

### Optional: starter module

For grid workspace keys (`R*10+C`), continuous zoom on the wheel, and opt-in contexts, copy [examples/gridoverview.lua](../../examples/gridoverview.lua) to `~/.config/hypr/gridoverview.lua` and `require("gridoverview")` from `hyprland.lua`. Set `settings.mainMod` if your main modifier is not Super. Contexts stay opt-in (`useContexts`, `context_labels`, …).

**Layout:** Grid Overview is designed for Hyprland's **Dwindle** layout. Edge-insert while dragging relies on Dwindle splits; other layouts are untested for that path.

## Building from source

1. Clone and enter the repository:

   ```bash
   git clone https://github.com/ismay7/hyprland-grid-overview.git
   cd hyprland-grid-overview
   ```

2. Build (headers must match the running Hyprland):

   ```bash
   make all
   ```

3. Load into the running session:

   ```bash
   hyprctl plugin load "$(pwd)/gridoverview.so"
   ```

4. Prefer `hyprpm` for updates, or from Lua:

   ```lua
   local pluginPath = "/absolute/path/to/hyprland-grid-overview/gridoverview.so"
   hl.plugin.load(pluginPath)
   ```

5. Add the same **toggle bind** as in step 5 above.
