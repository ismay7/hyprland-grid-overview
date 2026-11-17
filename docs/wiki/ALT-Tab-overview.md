Grid Overview can act as a visual `ALT + Tab` switcher. Pressing `ALT + Tab` opens the overview and uses `navigate("right")` to advance. Navigation stays on the monitor where the overview was opened and skips windows that are both pinned and floating.

The dispatcher does not wrap at the end. The module therefore compares the active workspace and window before and after moving right. If neither changes, the selection is already at the last cell on that monitor, so it navigates left until it reaches the first workspace and its first window.

Create `~/.config/hypr/scripts/alttab.lua` with the following module:

```lua
local M = {}

local openedByAltTab = false

local function object_value(value, key)
    if value == nil then
        return nil
    end

    local ok, result = pcall(function()
        return value[key]
    end)

    if ok then
        return result
    end
end

local function active_ids()
    local ws = hl.get_active_workspace()
    local win = hl.get_active_window()
    return object_value(ws, "id"), object_value(win, "address")
end

function M.next()
    if not (hl.plugin and hl.plugin.gridoverview) then
        return
    end

    if not openedByAltTab then
        openedByAltTab = true
        hl.plugin.gridoverview.overview("on")
    end

    local beforeWs, beforeWin = active_ids()
    hl.plugin.gridoverview.navigate("right")
    local afterWs, afterWin = active_ids()

    if beforeWs == afterWs and beforeWin == afterWin then
        for _ = 1, 64 do
            hl.plugin.gridoverview.navigate("left")
            local ws, win = active_ids()
            if ws == beforeWs and win == beforeWin then
                break
            end
            beforeWs, beforeWin = ws, win
        end
    end
end

function M.release()
    if openedByAltTab and hl.plugin and hl.plugin.gridoverview then
        openedByAltTab = false
        hl.plugin.gridoverview.overview("off")
    end
end

return M
```

Wire it from `hyprland.lua`:

```lua
local altTab = require("scripts.alttab")

hl.bind("ALT + Tab", altTab.next, { submap_universal = true })
hl.bind("ALT", altTab.release, { release = true, submap_universal = true })
```

Adjust the require path to match where you saved the module.
