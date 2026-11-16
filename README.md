# hyprland-grid-overview

**See Hyprland, then shape it.** A workspace overview plugin for Hyprland (plugin name `gridoverview`) that shows
your workspaces, windows and scratchpads as one grid you can grab with the mouse.

Hyprland can do a lot: as many workspaces as you like, scratchpads, floating windows, groups. Most of it sits behind
keybinds you have to know first. gridoverview puts it on screen instead: open the overview and everything is
there to drag, drop and click, so you can find out what Hyprland can do before you learn a single shortcut.
Nothing has to be configured first; every feature works out of the box and can be refined later — names for rows,
wallpapers, keybinds, animations. **Building up, never required.** The keyboard stays first-class: Hyprland's own
keybinds keep working, and the overview's views and zoom have dispatchers to bind.

hyprland-grid-overview is a fork of [hyprland-scroll-overview](https://github.com/yayuuu/hyprland-scroll-overview)
by Vaxry and yayuuu, a niri-like overview plugin. Everything the original does still works; the grid
features are additions on top of it (see [Credits](#credits)).

## Out of the box

- **A free grid**: workspace `R*10+C` sits in row `R`, column `C` (11–19, 21–29, …). It grows as you need it:
  every row gets an empty workspace past its last used one, and below the last used row there is an empty row,
  so a window can always be dropped somewhere new. Unused ones disappear again, also while the overview stays
  open.
- **Drag windows anywhere**: onto another workspace, onto the edge of a window to insert it on that side (dwindle;
  the preview shows the half it will take), onto its middle to swap both. Holding a dragged window at a screen
  edge scrolls to the neighbouring workspace.
- **Scratchpads at hand**: a tray at the right screen edge shows Hyprland's special workspaces as live cards
  with the icons of their apps, starting with the default one, `special`. Drag a window into a card or out of it onto any workspace; a click
  shows that scratchpad. New cards are anonymous by default (`scratch`, `scratch-2`, … — no name in the pill); the
  pencil opens a centred rename field (Enter confirms, Escape cancels). × closes their windows and removes them from the
  persistent roster (`~/.local/state/gridoverview/special_workspaces`; config `special_workspaces` only seeds it once).
  The "+" stays when every card is gone. The grip reorders (or Alt+drag; order in `tray_order`). Drop on "+" can
  auto-name by app or desktop category via `special_tray_auto_name` (default `off`). UI icons are SVGs under
  `assets/icons/`, embedded in the plugin. In a crowded card, take a window by its app icon: drag to move it, click to
  show the scratchpad with that window in front. A dragged window shrinks to card size as it approaches the tray; at
  the right edge the tray comes first, pushing on to the very edge scrolls on behind it.
- **Two views and everything in between**: buttons at the top switch between the centred view and the whole
  grid; the slider between them zooms continuously.
- **Mouse everywhere**: click a workspace or window to go there, wheel through the grid (`SHIFT` or the
  horizontal wheel for the other axis), pan freely with the middle mouse button (snaps on release); a middle
  click without moving closes the window under the pointer, like a browser tab (also in the scratchpad tray).

## Make it yours

Everything below is optional; see [Configuration](#configuration-lua) for the options.

- **Name your rows** (contexts, `context_names`, `context_labels`): labels next to the rows, which you can click,
  and with the example Lua module a wallpaper and a monitor per context and a bar widget. Rows without a
  name keep working the same. Contexts as columns instead of rows: `grid_orientation = "columns"`.
- **Looks**: only the selected workspace with wallpaper and shadow (`highlight_selected`), empty workspaces always,
  only while dragging or only under the pointer (`empty_marker`), app icons (`window_icons`) and action chrome
  (`window_chrome`) on the windows, own curves for moving and switching views (`transition_bezier`,
  `view_switch_bezier`).
- **Scratchpad tray**: cards that are always there, also while empty (`special_workspaces`), their size
  (`special_tray_width`), or hidden until the pointer touches the right edge, like an auto-hiding dock
  (`special_tray_autohide`).
- **Keys**: `overview("zoomout" | "zoomin")` (e.g. on `SUPER + CTRL + wheel`) zooms from the desktop into the
  centred view and on to the whole grid, stays where released and is remembered; `overview("fit" | "center" |
  "togglefit")` for the views. The example module adds keys for moving through the grid (with or without the
  active window).
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
| `grid_rows` / `grid_columns` | int | highest row / position within a row that gets placeholders (1–9; `grid_rows = 1` for a single row) | `9` |
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
| `grid_all_contexts` | bool | every named context (`context_names`) always has its first workspace in the overview; below the last used row there is always one empty row anyway (up to `grid_rows`) | `false` |
| `context_labels` | bool | name of every context next to its first workspace (left of it for rows, above for columns) | `false` |
| `context_names` | string | comma-separated context names, context 1 first | empty |
| `context_label_size` | int | label font size in layout px, independent of the zoom | `20` |
| `context_label_background` | color | pill behind the labels | `rgba(0,0,0,0.45)` |
| `context_label_color` | color | label colour (the current context's label is fully opaque, others dimmed) | `rgba(255,255,255,0.9)` |
| `window_icons` | bool | app icon on a rounded plate in the middle of every window | `false` |
| `window_chrome` | bool | float / maximize / fullscreen / pin / close buttons under the focused or hovered window (and pin-rail icons) | `true` |
| `window_icon_size` | int | icon size on screen in layout px, independent of the zoom (at most 60 % of a window's short side incl. plate) | `64` |
| `window_icon_plate_color` | color | plate behind the icon | `rgba(0,0,0,0.45)` |
| `icon_theme` | string | icon theme searched before hicolor (e.g. `Papirus-Dark`); empty = hicolor only | empty |
| `special_tray` | bool | special workspaces (scratchpads) as cards at the right screen edge; the whole-grid view keeps that space free | `true` |
| `special_tray_icons` | bool | app icons on the bottom edge of the cards, right-aligned like macOS Stage Manager (one per app, as many as fit, the name moves below them; drag an icon to move the app's topmost window, click it to show the scratchpad with that window focused; theme and plate colour from `icon_theme` / `window_icon_plate_color`) | `true` |
| `special_tray_blur` | bool | blur behind the tray's dock (with Hyprland's `decoration:blur` settings); off: a solid dark dock | `true` |
| `special_tray_icon_size` | int | size of those icons in layout px | `36` |
| `special_tray_autohide` | bool | hide the tray until the pointer touches the right screen edge, like an auto-hiding dock; it stays while the pointer is over it, also while a window is dragged. The whole-grid view keeps no space free | `false` |
| `special_workspaces` | string | seeds the persistent tray roster once (`~/.local/state/gridoverview/special_workspaces`); afterwards × / + / rename edit that file. `special` is Hyprland's unnamed scratchpad | `special` |
| `special_tray_auto_name` | string | name when dropping a window on +: `off` (anonymous `scratch-N`, default), `category` (desktop `Categories=`, else app), or `app` | `off` |
| `special_tray_width` | int | card width in layout px (cards shrink when they do not fit the screen height) | `300` |
| `transition_bezier` | string | bezier for moving between workspaces in the overview; empty = `windowsMove` | empty |
| `transition_speed` | float | speed for `transition_bezier` | `4` |
| `view_switch_bezier` | string | bezier for switching between centred and whole-grid view; empty = built-in smooth curve | empty |
| `view_switch_speed` | float | speed for `view_switch_bezier` | `3.5` |
| `exec_on_open` / `exec_on_close` | string | shell command when the overview opens / starts closing | empty |

The options, dispatchers, gestures and submaps of the original are documented in [docs/wiki](docs/wiki)
(adapted to the `gridoverview` names).

## Testing

`python3 scripts/selftest.py` runs a self-test in the current Hyprland session: it drives a virtual mouse and
keyboard (python-evdev, write access to `/dev/uinput`) on its own test window on workspace 91 (`--workspace`) and
checks keys of the example module, views and zoom, a cancelled drag and the scratchpad tray (reveal, drop on "+",
icon click and drag, card reorder by grip, card click, middle click closing a window). Your windows stay where they are; exit code 0 when everything passed. For scripts,
`hl.plugin.gridoverview.tray_layout()` returns the tray of the overview under the pointer as JSON.

## Known limitations

- Options changed at runtime with `hyprctl eval 'hl.config({ plugin = { gridoverview = { ... } } })'` reach the
  plugin only if they are already set in the config file (and for strings not even then); after a config reload
  everything applies.
- Dragging windows across monitors (`cross_monitor_drag`, from the original) has not been tested together with the
  scratchpad tray.

## Credits

hyprland-grid-overview is based on [hyprland-scroll-overview](https://github.com/yayuuu/hyprland-scroll-overview)
by **Vaxry** and **yayuuu** (BSD 3-Clause, Copyright (c) 2023, Hypr Development), which this fork keeps
under the same license. Thanks to them for the overview this is built on.
