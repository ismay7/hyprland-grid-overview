Dispatchers are runtime actions exposed by gridoverview. They let you open, close, or toggle the overview, switch views and zoom, move the selection between windows and workspaces, and act on the window under the pointer.

Call them from Lua with `hl.plugin.gridoverview.<dispatcher>(...)`, or from the shell with `hyprctl dispatch`.

## Usage examples

### Keybind

```lua
hl.bind("SUPER + D", function()
    if hl.plugin and hl.plugin.gridoverview then
        hl.plugin.gridoverview.overview("toggle")
    end
end)

-- Continuous zoom (e.g. SUPER + CTRL + wheel), as in the starter module:
hl.bind("SUPER + CTRL + mouse_down", function()
    hl.plugin.gridoverview.overview("zoomin")
end, { mouse = true })
hl.bind("SUPER + CTRL + mouse_up", function()
    hl.plugin.gridoverview.overview("zoomout")
end, { mouse = true })
```

For navigation binds while the overview is open, see [Keybind submap](Keybind-submap.md). Without a submap, arrow keys, Enter and Escape are built in.

### CLI

```bash
hyprctl dispatch 'hl.plugin.gridoverview.overview("toggle")'
hyprctl dispatch 'hl.plugin.gridoverview.overview("fit")'
hyprctl dispatch 'hl.plugin.gridoverview.navigate("left")'
hyprctl dispatch 'hl.plugin.gridoverview.window("close")'
```

The same actions are also available over IPC as `gridoverview:overview`, `gridoverview:navigate`, and `gridoverview:window` (e.g. for scripts via `hyprctl dispatch`).

## Available dispatchers

### `hl.plugin.gridoverview.overview`

| option | description |
| --- | --- |
| `toggle [monitor\|all]` | toggle the active monitor, a named monitor, or every monitor |
| `open` / `on` `[monitor\|all]` | open on the active monitor, a named monitor, or every monitor |
| `close` / `off` `[monitor\|all]` | close every overview by default, or only the named monitor |
| `select` | select the workspace under the cursor |
| `fit` | switch to the whole-grid view |
| `center` | switch to the centred view |
| `togglefit` | toggle between centred and whole-grid view |
| `zoomin` | continuous zoom in (desktop → centred → whole grid); level is remembered |
| `zoomout` | continuous zoom out |

Bare `toggle` targets the active monitor; `toggle all` opens any missing overviews or closes them when all are open. Bare `close` closes all open overviews.

### `hl.plugin.gridoverview.navigate`

Moves the selection between windows on the current workspace, then to the neighbouring grid cell in that direction when there are no more windows that way.

| option | description |
| --- | --- |
| `left` / `right` / `up` / `down` | move selection in that direction |

### `hl.plugin.gridoverview.window`

| option | description |
| --- | --- |
| `select` | focus/select the window under the mouse cursor |
| `close` | close the window under the mouse cursor |

[Back to Configuration](Configuration.md)
