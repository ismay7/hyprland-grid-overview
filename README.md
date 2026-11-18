# hyprland-grid-overview

**See Hyprland, then shape it.** A workspace overview plugin for Hyprland **0.57.2+** (plugin name `gridoverview`)
that shows your workspaces, windows and scratchpads as one grid you can grab with the mouse.

Hyprland can do a lot: as many workspaces as you like, scratchpads, floating windows, groups, pins. Most of it sits
behind keybinds you have to know first. gridoverview puts those core features on screen instead: open the overview and
reach them with the mouse and modifiers, so you can find out what Hyprland can do before you learn a single shortcut.
Nothing has to be configured first — free grid, groups, pins, scratchpad tray and zoom work out of the box. Refine later
if you want: looks, keys, animations, optional named rows. **Building up, never required.** The keyboard stays
first-class: Hyprland's own keybinds keep working, and the overview's views and zoom have dispatchers to bind.

https://github.com/user-attachments/assets/ba26646c-4223-4072-9be3-f75153c58a96

## Out of the box

- **A free grid**: workspace `R*10+C` sits in row `R`, column `C` (11–19, 21–29, …). It grows as you need it:
  every row gets an empty workspace past its last used one, and below the last used row there is an empty row,
  so a window can always be dropped somewhere new. Unused ones disappear again, also while the overview stays
  open.
- **Drag windows anywhere**: onto another workspace, onto the edge of a window to insert it on that side (dwindle;
  the preview shows the half it will take), onto its middle to swap both. Holding a dragged window at a screen
  edge scrolls to the neighbouring workspace.
- **Groups**: tiled windows show a tab strip in the overview. Click a tab to switch member; drag a tab away to detach
  it; drop a window onto a group's tabs (or the top of a grouped card) to join. The window chrome's group button
  toggles grouping; dissolving a group rebuilds the layout.
- **Pins**: pin from the window chrome (2×3 dock under the focused/hovered window: float / maximize / fullscreen ·
  group / pin / close). Pinned floating windows sit as icons on a pin rail on the focused workspace card — hover for
  chrome, drag them like any other window.
- **Scratchpads at hand**: a tray at the screen edge (default right, auto-hiding until the pointer touches it) shows Hyprland's special
  workspaces as live cards with the icons of their apps, starting with the default one, `special`. Drag a window into a card or out of it
  onto any workspace; a click shows that scratchpad. New cards are anonymous by default (`scratch`, `scratch-2`, … — no name in the pill); the
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

- **Name your rows (opt-in contexts)**: turn on `context_labels` (and usually `useContexts` in the example module).
  Labels sit next to the first workspace of each row; the pencil renames them into
  `~/.local/state/gridoverview/context_names` (survives restarts). Config holds structure only (wallpaper /
  optional monitor per row), not the display names. `grid_all_contexts` keeps those structural rows visible even
  while empty. Contexts as columns: `grid_orientation = "columns"`.
- **Looks**: only the selected workspace with wallpaper and shadow (`highlight_selected`), empty workspaces always,
  only while dragging or only under the pointer (`empty_marker`), app icons (`window_icons`), or turn off the action
  chrome (`window_chrome`, on by default). Own curves for moving and switching views (`transition_bezier`,
  `view_switch_bezier`).
- **Scratchpad tray**: which edge (`special_tray_edge`), card size (`special_tray_width`), always-visible instead of
  auto-hide (`special_tray_autohide = false`), and which cards seed the roster (`special_workspaces`).
- **Keys**: `overview("zoomout" | "zoomin")` (e.g. on `SUPER + CTRL + wheel`) zooms from the desktop into the
  centred view and on to the whole grid, stays where released and is remembered; `overview("fit" | "center" |
  "togglefit")` for the views. The example module adds keys for moving through the grid (with or without the
  active window).
- **Hooks**: shell commands on open/close (e.g. to change a dock's auto-hide while the overview is open).

## Installation

```bash
hyprpm add https://github.com/ismay7/hyprland-grid-overview
hyprpm update
hyprpm enable gridoverview
```

Requires Hyprland **0.57.2+**. Lua config only. Load on startup with `hyprpm reload` from `hyprland.start`, then
**bind a toggle** — required; the plugin has no keybinds of its own:

```lua
hl.bind("SUPER + D", function()  -- your main modifier + any key
    if hl.plugin and hl.plugin.gridoverview then
        hl.plugin.gridoverview.overview("toggle")
    end
end)
```

Until that bind exists, you cannot open the overview from the keyboard. After it does: free grid and the other defaults are on. Optional headstart for grid keys / zoom / contexts:
[examples/gridoverview.lua](examples/gridoverview.lua) → `~/.config/hypr/gridoverview.lua` + `require("gridoverview")`.
Details: [docs/wiki/Installation.md](docs/wiki/Installation.md).

Manual build: `make all` → `gridoverview.so` (headers must match the running Hyprland).

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
                empty_marker = "hover",
                zoom_follow_mouse = true,
                window_icons = true,
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
is a complete module for that: grid keys, overview + continuous zoom, scratchpad tray options, and optional
contexts (wallpapers / labels / `grid_all_contexts`) behind explicit flags. Save it as
`~/.config/hypr/gridoverview.lua` and add `require("gridoverview")` to `hyprland.lua`.

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
| `grid` | bool | grid layout: workspace `R*10+C` in row `R`, column `C` | `true` |
| `grid_orientation` | string | contexts (workspaces `R*10+C` sharing `R`) as `rows` or `columns` | `rows` |
| `grid_rows` / `grid_columns` | int | highest row / position within a row that gets placeholders (1–9; `grid_rows = 1` for a single row) | `9` |
| `grid_buttons` | bool | buttons to switch between centred and whole-grid view | `true` |
| `highlight_selected` | bool | wallpaper + shadow only on the selected workspace; empty ones as outline + plus | `false` |
| `highlight_padding` | int | how far (layout px) the highlight reaches past the work area | `0` |
| `empty_marker` | string | when empty workspaces show their outline: `always`, `drag` (while dragging), `hover` (only under the pointer or a dragged window; the selected one always) | `hover` |
| `empty_color` | color | outline and plus colour | `rgba(255,255,255,0.35)` |
| `empty_halo_color` | color | thin halo under outline and plus, for light wallpapers | `rgba(0,0,0,0.4)` |
| `empty_color_from_border` | bool | use `general:col.active_border` (e.g. matugen) with `empty_color`'s alpha | `false` |
| `shadow:active_color` | color | shadow colour of the selected workspace | unset |
| `zoom_step` | float | continuous zoom: zoom levels per wheel notch (0 = desktop, 1 = centred view, 2 = whole grid) | `0.25` |
| `zoom_follow_mouse` | bool | continuous zoom: zooming in selects the workspace under the cursor and zooms towards it | `true` |
| `grid_all_contexts` | bool | always show the first workspace of each structural context slot (`context_count`), even while empty; below the last used row there is always one empty row anyway (up to `grid_rows`) | `false` |
| `context_count` | int | how many context rows the config reserved (wallpaper/monitor bindings); used with `grid_all_contexts` | `0` |
| `context_labels` | bool | name ··· pencil next to each context's first workspace (left for rows, above for columns); rename → `~/.local/state/gridoverview/context_names` | `false` |
| `context_names` | string | legacy unused seed; labels live only in the state file | empty |
| `context_label_size` | int | label font size in layout px, independent of the zoom | `20` |
| `context_label_background` | color | pill behind the labels | `rgba(0,0,0,0.45)` |
| `context_label_color` | color | label colour (the current context's label is fully opaque, others dimmed) | `rgba(255,255,255,0.9)` |
| `window_icons` | bool | app icon on a rounded plate in the middle of every window | `true` |
| `window_chrome` | bool | 2×3 action dock under the focused/hovered window (float / maximize / fullscreen · group / pin / close; also on pin-rail icons) | `true` |
| `window_icon_size` | int | icon size on screen in layout px, independent of the zoom (at most 60 % of a window's short side incl. plate) | `64` |
| `window_icon_plate_color` | color | plate behind the icon | `rgba(0,0,0,0.45)` |
| `icon_theme` | string | icon theme searched before hicolor (e.g. `Papirus-Dark`); empty = hicolor only | empty |
| `special_tray` | bool | special workspaces (scratchpads) as cards at a screen edge; the whole-grid view keeps that space free when the tray is shown | `true` |
| `special_tray_edge` | string | which edge holds the tray: `right`, `left`, or `bottom` | `right` |
| `special_tray_icons` | bool | app icons on the bottom edge of the cards, right-aligned like macOS Stage Manager (one per app, as many as fit, the name moves below them; drag an icon to move the app's topmost window, click it to show the scratchpad with that window focused; theme and plate colour from `icon_theme` / `window_icon_plate_color`) | `true` |
| `special_tray_blur` | bool | blur behind the tray's dock (with Hyprland's `decoration:blur` settings); off: a solid dark dock | `true` |
| `special_tray_icon_size` | int | size of those icons in layout px | `36` |
| `special_tray_autohide` | bool | hide the tray until the pointer touches its screen edge, like an auto-hiding dock; it stays while the pointer is over it, also while a window is dragged. The whole-grid view keeps no space free | `true` |
| `special_workspaces` | string | seeds the persistent tray roster once (`~/.local/state/gridoverview/special_workspaces`); afterwards × / + / rename edit that file. `special` is Hyprland's unnamed scratchpad | `special` |
| `special_tray_auto_name` | string | name when dropping a window on +: `off` (anonymous `scratch-N`, default), `category` (desktop `Categories=`, else app), or `app` | `off` |
| `special_tray_width` | int | card width in layout px (cards shrink when they do not fit the screen height) | `300` |
| `transition_bezier` | string | bezier for moving between workspaces in the overview; empty = `windowsMove` | empty |
| `transition_speed` | float | speed for `transition_bezier` | `4` |
| `view_switch_bezier` | string | bezier for switching between centred and whole-grid view; empty = built-in smooth curve | empty |
| `view_switch_speed` | float | speed for `view_switch_bezier` | `3.5` |
| `exec_on_open` / `exec_on_close` | string | shell command when the overview opens / starts closing | empty |

Options, dispatchers, gestures and further setup are documented in the
[wiki](https://github.com/ismay7/hyprland-grid-overview/wiki) (sources under [docs/wiki](docs/wiki)).

## Testing

`python3 scripts/selftest.py` runs a self-test in the current Hyprland session: it drives a virtual mouse and
keyboard (python-evdev, write access to `/dev/uinput`) on its own test window on workspace 91 (`--workspace`) and
checks keys of the example module, views and zoom, a cancelled drag and the scratchpad tray (reveal, drop on "+",
icon click and drag, card reorder by grip, card click, middle click closing a window). Your windows stay where they are; exit code 0 when everything passed. For scripts,
`hl.plugin.gridoverview.tray_layout()` returns the tray of the overview under the pointer as JSON (`cards[].visual`
for the foreshortened UI silhouette, `perspective_depth`, plus `add` / icons / chrome hitboxes).

## Known limitations

- **Designed for Dwindle.** Edge-insert while dragging (split preview, drop onto a window side) uses Dwindle's
  preselect/split path. Other Hyprland layouts (Master, …) are not a supported target: the overview still opens and
  you can move windows between workspaces/scratchpads, but tiled drop-into-split behaviour is untested and may be
  wrong or no-op.
- **Trackpad gestures** (`gesture` / `gesture_distance`) ship from upstream but are not dogfooded here — no gesture
  hardware in daily maintainer use. The API stays; feedback from people who can test it is welcome.
- Options changed at runtime with `hyprctl eval 'hl.config({ plugin = { gridoverview = { ... } } })'` reach the
  plugin only if they are already set in the config file (and for strings not even then); after a config reload
  everything applies.
- Dragging windows across monitors (`cross_monitor_drag`) has not been tested together with the scratchpad tray.

## Credits

hyprland-grid-overview is based on [hyprland-scroll-overview](https://github.com/yayuuu/hyprland-scroll-overview)
by **Vaxry** and **yayuuu** (BSD 3-Clause, Copyright (c) 2023, Hypr Development), which this fork keeps
under the same license. Thanks to them for the overview this is built on.
