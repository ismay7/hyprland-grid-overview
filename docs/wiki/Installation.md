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

4. To load enabled plugins automatically when Hyprland starts, run `hyprpm reload` from the `hyprland.start` event handler in your Lua configuration:

   ```lua
   hl.on("hyprland.start", function()
       hl.exec_cmd("hyprpm reload")
   end)
   ```

5. Configure and enjoy. A complete starter module lives at [examples/gridoverview.lua](../../examples/gridoverview.lua) — save it as `~/.config/hypr/gridoverview.lua` and `require("gridoverview")` from `hyprland.lua`. Contexts (named rows, labels, wallpapers) are **opt-in** in that module (`useContexts`, `context_labels`, …); the free grid works without them.

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

4. To load the plugin automatically when Hyprland starts, add it to your Lua configuration using an absolute path — or prefer `hyprpm` so updates stay consistent:

   ```lua
   local pluginPath = "/absolute/path/to/hyprland-grid-overview/gridoverview.so"
   hl.plugin.load(pluginPath)
   ```
