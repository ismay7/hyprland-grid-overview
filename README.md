# hyprland-grid-overview

A workspace **grid** overview for Hyprland (plugin name `gridoverview`): workspaces are arranged as a two-dimensional grid of
*contexts* (rows) and *workspaces* (columns), e.g. row 1 = general work, row 2 = game dev,
row 3 = communication. Workspace `R*10+C` sits in row `R`, column `C` (11–19, 21–29, …).

hyprland-grid-overview is a fork of [hyprland-scroll-overview](https://github.com/yayuuu/hyprland-scroll-overview)
by Vaxry and yayuuu, a niri-like overview plugin. Everything the original does still works; the grid
features are additions on top of it (see [Credits](#credits)).

## Features

- **Grid layout** (`grid = true`): rows are contexts, columns are workspaces; arrow keys, the mouse wheel
  (`SHIFT` or horizontal wheel for columns) and the middle mouse button (free 2D pan, snaps on release)
  move through it.
- **Placeholders**: while the overview is open every row gets an empty workspace past its last used column,
  plus an empty row below the last used one, so windows can be dropped there. Unused placeholders disappear
  again, also while the overview stays open.
- **Directional drop** for dwindle: dropping a window on the edge of another one inserts it on that side
  (preview: the half it will take), dropping it in the middle swaps both.
- **Edge scrolling**: holding a dragged window at a screen edge steps to the neighbouring workspace. On the right
  the scratchpad tray comes first (it slides in with `special_tray_autohide`); pushing on to the very edge, beyond
  its cards, and holding there scrolls on behind it.
- **Selected-workspace highlight** (`highlight_selected = true`): only the selected workspace gets wallpaper
  and shadow, sliding between workspaces; the others show just their windows, empty ones an outline with a
  plus.
- **Whole-grid view**: buttons at the top switch between the centred view and a view that fits the whole
  grid on screen (also `overview("fit" | "center" | "togglefit")`); a slider between them zooms
  continuously from one to the other.
- **Continuous zoom**: `overview("zoomout" | "zoomin")` (e.g. on `SUPER + CTRL + wheel`) zooms from the desktop
  into the centred view and on to the whole grid; the zoom stays where it is released and is remembered,
  zooming all the way in closes the overview.
- **Scratchpad tray** (on by default): special workspaces as cards at the right screen edge, outside
  the zoomed grid. Drag windows into a card (an unused configured name is created on the fly) or out of it onto
  any workspace; a click on a card closes the overview and shows that scratchpad. Names listed in
  `special_workspaces` always get a card, also while empty. With `special_tray_autohide` it slides in only when
  the pointer touches the right edge.
- **Hooks**: shell commands on open/close (e.g. to change a dock's auto-hide while the overview is open).

## Installation

```bash
hyprpm add https://github.com/ismay7/hyprland-grid-overview
hyprpm enable gridoverview
```

The repository is private for now, so cloning needs access to it (SSH key or token).

For a manual build: `make all` produces `gridoverview.so` (needs the Hyprland headers of the running
version).

## Configuration (Lua)

```lua
if hl.plugin and hl.plugin.gridoverview then
    hl.config({
        plugin = {
            gridoverview = {
                scale = 0.5,
                workspace_gap = 60,
                grid = true,
                grid_rows = 4,
                grid_columns = 9,
                wallpaper = 2,
                blur = true,
                shadow = { enabled = true, range = 50 },
                highlight_selected = true,
                highlight_padding = 40,
                empty_marker = "always",
                transition_bezier = "easeOutQuint",
                transition_speed = 3.0,
            },
        },
    })
end

hl.bind("SUPER + D", function()
    if hl.plugin and hl.plugin.gridoverview then
        hl.plugin.gridoverview.overview("toggle")
    end
end)
```

Moving between workspaces outside the overview (left/right within a context, up/down between contexts)
is plain Hyprland config: focus/move to `row * 10 + column`. [examples/gridoverview.lua](examples/gridoverview.lua)
is a complete module for that: contexts, grid keys, optional (togglable) wallpaper per context, the overview and its
continuous zoom, and all plugin options in one settings table. Save it as `~/.config/hypr/gridoverview.lua`
and add `require("gridoverview")` to `hyprland.lua`.

### Multiple monitors

Workspace ids are global in Hyprland, so the grid is shared: every monitor's overview shows the contexts that
live on it. A context belongs to the monitor a workspace rule binds its first workspace (`R1`) to; without a rule
to the monitor that already has workspaces of it, and a context without any workspace to the monitor of
workspace 11. Placeholders are only created for the overview's own contexts. With the example module, bind a
context with `monitor = "HDMI-A-1"` in its entry; switching to it then moves the focus to that monitor, and new
workspaces of a context are created on its monitor. A separate grid per monitor is not supported (yet).

### Options added by this fork

| option | type | description | default |
| --- | --- | --- | --- |
| `grid` | bool | grid layout: workspace `R*10+C` in row `R`, column `C` | `false` |
| `grid_orientation` | string | contexts (workspaces `R*10+C` sharing `R`) as `rows` or `columns` | `rows` |
| `grid_rows` / `grid_columns` | int | highest context / position within a context that gets placeholders (1–9) | `9` |
| `grid_buttons` | bool | buttons to switch between centred and whole-grid view | `true` |
| `highlight_selected` | bool | wallpaper + shadow only on the selected workspace; empty ones as outline + plus | `false` |
| `highlight_padding` | int | how far (layout px) the highlight reaches past the work area | `0` |
| `empty_marker` | string | when empty workspaces show their outline: `always`, `drag` (while dragging), `hover` (only under the pointer or a dragged window; the selected one always) | `always` |
| `empty_color` | color | outline and plus colour | `rgba(255,255,255,0.35)` |
| `empty_halo_color` | color | thin halo under outline and plus, for light wallpapers | `rgba(0,0,0,0.4)` |
| `empty_color_from_border` | bool | use `general:col.active_border` (e.g. matugen) with `empty_color`'s alpha | `false` |
| `shadow:active_color` | color | shadow colour of the selected workspace | unset |
| `zoom_step` | float | continuous zoom: zoom levels per wheel notch (0 = desktop, 1 = centred view, 2 = whole grid) | `0.25` |
| `zoom_follow_mouse` | bool | continuous zoom: zooming in selects the workspace under the cursor and zooms towards it | `false` |
| `grid_all_contexts` | bool | every context up to `grid_rows` always has its first workspace in the overview | `false` |
| `context_labels` | bool | name of every context next to its first workspace (left of it for rows, above for columns) | `false` |
| `context_names` | string | comma-separated context names, context 1 first | empty |
| `context_label_size` | int | label font size in layout px, independent of the zoom | `20` |
| `context_label_background` | color | pill behind the labels | `rgba(0,0,0,0.45)` |
| `context_label_color` | color | label colour (the current context's label is fully opaque, others dimmed) | `rgba(255,255,255,0.9)` |
| `window_icons` | bool | app icon on a round plate in the middle of every window | `false` |
| `window_icon_size` | int | icon size on screen in layout px, independent of the zoom (at most 60 % of a window's short side incl. plate) | `64` |
| `window_icon_plate_color` | color | plate behind the icon | `rgba(0,0,0,0.45)` |
| `icon_theme` | string | icon theme searched before hicolor (e.g. `Papirus-Dark`); empty = hicolor only | empty |
| `special_tray` | bool | special workspaces (scratchpads) as cards at the right screen edge; the whole-grid view keeps that space free | `true` |
| `special_tray_autohide` | bool | hide the tray until the pointer touches the right screen edge, like an auto-hiding dock; it stays while the pointer is over it, also while a window is dragged. The whole-grid view keeps no space free | `false` |
| `special_workspaces` | string | comma-separated special workspace names (without `special:`) that always get a card, also while empty | empty |
| `special_tray_width` | int | card width in layout px (cards shrink when they do not fit the screen height) | `300` |
| `transition_bezier` | string | bezier for moving between workspaces in the overview; empty = `windowsMove` | empty |
| `transition_speed` | float | speed for `transition_bezier` | `4` |
| `view_switch_bezier` | string | bezier for switching between centred and whole-grid view; empty = built-in smooth curve | empty |
| `view_switch_speed` | float | speed for `view_switch_bezier` | `3.5` |
| `exec_on_open` / `exec_on_close` | string | shell command when the overview opens / starts closing | empty |

The options, dispatchers, gestures and submaps of the original are documented in [docs/wiki](docs/wiki)
(adapted to the `gridoverview` names).

## Credits

hyprland-grid-overview is based on [hyprland-scroll-overview](https://github.com/yayuuu/hyprland-scroll-overview)
by **Vaxry** and **yayuuu** (BSD 3-Clause, Copyright (c) 2023, Hypr Development), which this fork keeps
under the same license. Thanks to them for the overview this is built on.
