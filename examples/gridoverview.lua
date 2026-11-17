-- Example Hyprland (Lua config) module for hyprland-grid-overview.
--
-- Save as ~/.config/hypr/gridoverview.lua and add `require("gridoverview")` to hyprland.lua. Remove or
-- rebind your own workspace keys that clash with the ones below (mainMod + [1-9], mainMod + SHIFT + arrows, ...).
--
-- A free grid of workspaces: workspace id = row * 10 + position (row 1 = 11..19, 2 = 21..29, ...).
-- orientation "rows": the rows are rows, their workspaces side by side;
--             "columns": they are columns, their workspaces stacked (keys and overview follow).
--
-- Contexts are opt-in (useContexts / context_labels / grid_all_contexts / contextWallpapers). Labels are
-- renamed in the overview (pencil) and stored in ~/.local/state/gridoverview/context_names — not in this file.
--
-- Binds use settings.mainMod (default SUPER). Change it if your Hyprland main modifier is not Super — the plugin
-- itself has no keybinds; only this module does.
--
-- mainMod + SHIFT + arrow          next workspace / context in that direction, also onto the empty plus ones
--                                  (same position in the other context, or the last reachable one before it)
-- mainMod + CTRL + SHIFT + arrow   same, taking the active window along
-- mainMod + [1-9]                  workspace 1-9 of the current context (+ SHIFT: move window there)
-- mainMod + Tab / SHIFT + Tab      next/previous workspace of the current context, incl. the new empty one
-- mainMod + scroll                 same as Tab; mainMod + SHIFT + scroll: next/previous context
-- mainMod + D                      grid overview; mainMod + CTRL + scroll zooms into / out of it continuously

local settings = {
    mainMod     = "SUPER", -- change if your main modifier is not Super (e.g. "ALT")
    orientation = "rows",
    workspaces  = 9,  -- per context
    monitor     = "", -- monitor of the first workspace (11); empty = Hyprland's choice

    -- the grid is free: like a row gets an empty workspace past its last used one, the grid gets an empty row
    -- below the last used one, up to this many rows (1 = a single row, 9 at most)
    rows = 9,

    -- opt-in: false = free grid (default). true = use contexts{} for wallpapers / optional monitors
    useContexts = false,

    -- Structure only when useContexts (no names). monitor = "DP-2" pins that context's workspaces.
    contexts = {
        -- { wallpaper = "abstract/waves.jpg" },
        -- { wallpaper = "nature/forest.jpg", monitor = "HDMI-A-1" },
    },

    -- opt-in: switch wallpaper on context change (needs wallpaper entries above + a wallpaperCommand)
    contextWallpapers = false,
    wallpaperDir      = "~/Pictures/Wallpapers", -- base for relative wallpaper paths (absolute / ~ paths work too)
    -- %s = the wallpaper path, e.g.
    --   "swww img '%s'"
    --   "dms ipc call wallpaper set '%s'"   (Dux / DankMaterialShell)
    wallpaperCommand  = "swww img '%s'",
    -- with more than one monitor: per monitor; first %s = monitor, second %s = path, e.g.
    --   "swww img -o '%s' '%s'"
    --   "dms ipc call wallpaper setFor '%s' '%s'"
    wallpaperMonitorCommand = "swww img -o '%s' '%s'",

    -- plugin:gridoverview options (grid, grid_orientation, grid_rows and grid_columns are set from the above)
    overview = {
        scale = 0.5,
        workspace_gap = 40,
        wallpaper = 2,
        blur = true,
        shadow = {
            enabled = true,
            range = 50,
        },
        highlight_selected = true, -- wallpaper + shadow only on the selected workspace, empty ones as outline + plus
        highlight_padding = 40,
        empty_marker = "hover",    -- always | drag (while dragging) | hover (only under the pointer)
        zoom_step = 0.25,          -- continuous zoom per wheel notch (1 = desktop -> centred view)
        zoom_follow_mouse = true,  -- continuous zoom: zooming in goes towards the workspace under the cursor
        -- opt-in with useContexts: reserve first WS per contexts{} row; show name···pencil labels
        grid_all_contexts = false,
        context_labels = false,
        context_label_size = 20,
        window_icons = true,
        window_chrome = true,  -- float / maximize / fullscreen · group / pin / close (also on pin-rail icons)
        window_icon_size = 64,
        -- scratchpads (special workspaces) as cards at the right edge: drag windows in and out, click one to show it
        special_tray = true,
        special_workspaces = "special", -- seeds the persistent roster once (~/.local/state/gridoverview/special_workspaces)
        special_tray_auto_name = "off", -- off | category | app: name when dropping a window on +
        special_tray_width = 300,  -- card width in layout px
        special_tray_icons = true, -- app icons on the bottom edge of the cards
        special_tray_icon_size = 36,
        special_tray_autohide = true, -- hidden until the pointer touches the tray edge (also while dragging)
        transition_bezier = "",    -- e.g. a bezier defined in your config; empty = windowsMove animation
        transition_speed = 3.0,
        -- exec_on_open = "",
        -- exec_on_close = "",
    },
}

-- ---------------------------------------------------------------------------------------------------------------

local mainMod           = settings.mainMod or "SUPER"
local contexts          = settings.useContexts ~= false and settings.contexts or {}
local gridRows          = math.max(1, math.min(9, settings.rows or 9))
local gridCols          = settings.workspaces
local contextsAsColumns = settings.orientation == "columns"
local contextSlide      = contextsAsColumns and "slide" or "slidevert"
local workspaceSlide    = contextsAsColumns and "slidevert" or "slide"

-- empty drop targets in the overview are created by the plugin itself; only the first workspace is kept
local firstWorkspace = { workspace = "11", default = true, persistent = true }
if settings.monitor ~= "" then
    firstWorkspace.monitor = settings.monitor
end
hl.workspace_rule(firstWorkspace)

-- contexts with a monitor: all their workspaces live there, the first one is that monitor's default workspace
for index, context in ipairs(contexts) do
    if context.monitor and context.monitor ~= "" then
        for position = 1, gridCols do
            hl.workspace_rule({ workspace = tostring(index * 10 + position), monitor = context.monitor, default = position == 1 })
        end
    end
end

-- workspaces outside the grid (e.g. plain 1-10) count as context 1
local function gridPos()
    local ws = hl.get_active_workspace()
    local id = ws and ws.id or 11
    if id < 11 or id > gridRows * 10 + gridCols or id % 10 == 0 then
        return 1, math.max(1, math.min(id % 10, gridCols))
    end
    return math.floor(id / 10), id % 10
end

-- Hyprland picks the slide direction from the workspace ids; only the axis has to be set per switch.
-- Adjust speed / bezier to your own workspace animation.
local function gridSlide(style)
    for _, leaf in ipairs({ "workspaces", "workspacesIn", "workspacesOut" }) do
        hl.animation({ leaf = leaf, enabled = true, speed = 2.4, bezier = "default", style = style })
    end
end

-- multi-monitor: the monitor a context lives on - its monitor entry, else the monitor of its existing
-- workspaces, else the one of workspace 11 (nil: don't care)
local function contextMonitor(row)
    local context = contexts[row]
    if context and context.monitor and context.monitor ~= "" then
        return context.monitor
    end
    local main
    for _, ws in ipairs(hl.get_workspaces()) do
        if ws.id and not ws.special and ws.monitor then
            if ws.id >= 11 and ws.id % 10 ~= 0 and math.floor(ws.id / 10) == row then
                return ws.monitor.name
            end
            if ws.id == 11 then
                main = ws.monitor.name
            end
        end
    end
    return main
end

local function gridGoto(row, col, carry, style)
    gridSlide(style)
    local id     = row * 10 + col
    local window = carry and hl.get_active_window()

    -- Hyprland creates a new workspace on the focused monitor: focus the context's monitor first
    if not hl.get_workspace(id) then
        local target  = contextMonitor(row)
        local current = hl.get_active_monitor()
        if target and current and target ~= current.name then
            hl.dispatch(hl.dsp.focus({ monitor = target }))
            if window then
                hl.dispatch(hl.dsp.focus({ workspace = id }))
            end
        end
    end

    if window then
        hl.dispatch(hl.dsp.window.move({ workspace = id, window = "address:" .. window.address }))
    else
        hl.dispatch(hl.dsp.focus({ workspace = id }))
    end
end

-- Workspaces the keys may go to: those that exist, plus what the overview shows as empty plus: in each context
-- the positions up to one past its last used one, the first workspace of the row after the last used one (with
-- grid_all_contexts at least down to the last named context)
local function gridTargets()
    local lastCol, lastRow, existing = {}, 0, {}
    for _, ws in ipairs(hl.get_workspaces()) do
        local id = ws.id
        if id and not ws.special and id >= 11 and id % 10 ~= 0 and math.floor(id / 10) <= gridRows then
            existing[id] = true
            if ws.windows > 0 then
                local r = math.floor(id / 10)
                lastCol[r] = math.max(lastCol[r] or 0, id % 10)
                lastRow = math.max(lastRow, r)
            end
        end
    end

    -- one row past the last used one, and (grid_all_contexts) at least down to the last named context
    local maxRow = math.min(gridRows, math.max(lastRow + 1, settings.overview.grid_all_contexts and #contexts or 0))
    local function allowed(row, col)
        if existing[row * 10 + col] then return true end
        return row >= 1 and row <= maxRow and col >= 1 and col <= math.min((lastCol[row] or 0) + 1, gridCols)
    end
    return allowed, maxRow
end

-- same position in another context, or the last allowed one before it; nil if the context is not reachable
local function contextColumn(row, col, allowed)
    for c = col, 1, -1 do
        if allowed(row, c) then return c end
    end
    return nil
end

local function gridStep(dRow, dCol, carry)
    return function()
        local row, col = gridPos()
        local allowed = gridTargets()
        local newRow = math.max(1, math.min(gridRows, row + dRow))
        local newCol = math.max(1, math.min(gridCols, col + dCol))
        if dRow ~= 0 then
            newCol = contextColumn(newRow, col, allowed)
            if not newCol then return end
        elseif not allowed(newRow, newCol) then
            return
        end
        if newRow == row and newCol == col then return end
        gridGoto(newRow, newCol, carry, dRow ~= 0 and contextSlide or workspaceSlide)
    end
end

-- next/previous reachable workspace of the current context (incl. the new empty one at its end), wrapping around
local function gridCycle(step)
    return function()
        local row, col = gridPos()
        local allowed = gridTargets()
        for k = 1, gridCols - 1 do
            local c = (col - 1 + step * k) % gridCols + 1
            if allowed(row, c) then return gridGoto(row, c, false, workspaceSlide) end
        end
    end
end

-- switch the context wallpaper
local home         = os.getenv("HOME")
local wallpaperRow = { [""] = gridPos() } -- per monitor with more than one monitor, else under ""

local function wallpaperPath(path)
    if path:sub(1, 1) == "~" then
        return home .. path:sub(2)
    end
    if path:sub(1, 1) == "/" then
        return path
    end
    return (settings.wallpaperDir:gsub("^~", home):gsub("/$", "")) .. "/" .. path
end

hl.on("workspace.active", function()
    local row = gridPos()

    if not settings.contextWallpapers then
        return
    end

    -- with more than one monitor each monitor shows the wallpaper of its own context (wallpaperMonitorCommand)
    local workspace    = hl.get_active_workspace()
    local monitor      = workspace and workspace.monitor and workspace.monitor.name or ""
    local multiMonitor = #hl.get_monitors() > 1 and settings.wallpaperMonitorCommand ~= nil
    local key          = multiMonitor and monitor or ""
    if row == wallpaperRow[key] then
        return
    end
    wallpaperRow[key] = row

    local context = contexts[row]
    if not (context and context.wallpaper) then
        return
    end
    if multiMonitor then
        hl.exec_cmd(string.format(settings.wallpaperMonitorCommand, monitor, wallpaperPath(context.wallpaper)))
    elseif settings.wallpaperCommand then
        hl.exec_cmd(string.format(settings.wallpaperCommand, wallpaperPath(context.wallpaper)))
    end
end)

-- keys: { key, context delta, workspace delta }
local gridArrows = contextsAsColumns
    and { { "left", -1, 0 }, { "right", 1, 0 }, { "up", 0, -1 }, { "down", 0, 1 } }
    or  { { "left", 0, -1 }, { "right", 0, 1 }, { "up", -1, 0 }, { "down", 1, 0 } }
for _, dir in ipairs(gridArrows) do
    hl.bind(mainMod .. " + SHIFT + " .. dir[1],        gridStep(dir[2], dir[3], false))
    hl.bind(mainMod .. " + CTRL + SHIFT + " .. dir[1], gridStep(dir[2], dir[3], true))
end

for i = 1, gridCols do
    hl.bind(mainMod .. " + " .. i,         function() gridGoto(gridPos(), i, false, workspaceSlide) end)
    hl.bind(mainMod .. " + SHIFT + " .. i, function() gridGoto(gridPos(), i, true, workspaceSlide) end)
end

hl.bind(mainMod .. " + Tab", gridCycle(1))
hl.bind(mainMod .. " + SHIFT + Tab", gridCycle(-1))
hl.bind(mainMod .. " + mouse_down", gridCycle(1))
hl.bind(mainMod .. " + mouse_up",   gridCycle(-1))
hl.bind(mainMod .. " + SHIFT + mouse_down", gridStep(1, 0, false))
hl.bind(mainMod .. " + SHIFT + mouse_up",   gridStep(-1, 0, false))

-- hl.plugin.gridoverview only exists once hyprpm has loaded the plugin, so resolve it at keypress time
local function overview(action)
    return function()
        if hl.plugin and hl.plugin.gridoverview then
            hl.plugin.gridoverview.overview(action)
        end
    end
end

hl.bind(mainMod .. " + D", overview("toggle"))
hl.bind(mainMod .. " + CTRL + mouse_down", overview("zoomout"))
hl.bind(mainMod .. " + CTRL + mouse_up",   overview("zoomin"))

-- plugin.gridoverview keys are unknown until the plugin is loaded and would abort the config
if hl.plugin and hl.plugin.gridoverview then
    local options = {}
    for key, value in pairs(settings.overview) do
        options[key] = value
    end
    options.grid             = true
    options.grid_orientation = settings.orientation
    options.grid_rows        = gridRows
    options.grid_columns     = gridCols
    options.context_count    = #contexts
    options.context_names    = ""

    hl.config({ plugin = { gridoverview = options } })
end
