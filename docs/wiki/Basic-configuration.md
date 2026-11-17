After [Installation](Installation.md) (`hyprpm enable` + one toggle bind), the plugin defaults are already a usable Grid Overview. The snippet below is optional tuning. For grid workspace keys, continuous zoom binds and opt-in contexts, use the starter module at [examples/gridoverview.lua](../../examples/gridoverview.lua).

Out of the box, tiled **groups** show a tab strip (click to switch, drag a tab away to detach, drop onto tabs to join). **Pins** use the chrome pin button; pinned floaters sit on a pin rail on the focused workspace card. The 2×3 chrome dock (`window_chrome`) also covers float, maximize, fullscreen, group and close.

## Configuration

```lua
-- .config/hypr/hyprland.lua
hl.config({
    plugin = {
        gridoverview = {
            grid = true,
            grid_orientation = "rows", -- or "columns"
            grid_rows = 9,
            grid_columns = 9,
            scale = 0.5,
            workspace_gap = 40,
            wallpaper = 2, -- 0 global only, 1 per-workspace only, 2 both
            blur = true,
            shadow = {
                enabled = true,
                range = 50,
            },
            highlight_selected = true,
            highlight_padding = 40,
            empty_marker = "hover", -- always | drag | hover
            grid_buttons = true,
            zoom_step = 0.25,
            zoom_follow_mouse = true,
            window_icons = true,
            window_chrome = true,
            special_tray = true,
            special_workspaces = "special",
        },
    },
})

-- Toggle with your main modifier (starter module: settings.mainMod, default SUPER)
hl.bind("SUPER + D", function()
    if hl.plugin and hl.plugin.gridoverview then
        hl.plugin.gridoverview.overview("toggle")
    end
end)
```

With the default `grid = true`, workspace `R*10+C` sits in row `R`, column `C`. Empty plus-workspaces appear automatically while the overview is open; unused ones disappear again. Outside the overview, moving between cells is plain Hyprland: focus/move to `row * 10 + column` (the optional starter module binds that for you).

## Properties

| property | type | description | default |
| --- | --- | --- | --- |
| grid | bool | place workspace `R*10+C` in a free grid | `true` |
| grid_orientation | string | contexts as `rows` or `columns` | `rows` |
| grid_rows / grid_columns | int | highest row / column that gets placeholders (1–9) | `9` |
| grid_buttons | bool | buttons to switch centred ↔ whole-grid view | `true` |
| grid_all_contexts | bool | always show the first workspace of each structural context slot | `false` |
| context_count | int | how many context slots config reserved (with `grid_all_contexts`) | `0` |
| context_labels | bool | name ··· pencil next to each context's first workspace | `false` |
| gesture_distance | number | how far is the max for the gesture | `200` |
| scale | float | overview scale for the centred view, [0.1–0.9] | `0.5` |
| workspace_gap | number | gap between workspace cards, in layout px | `0` |
| cross_monitor_drag | bool | enable cross-monitor overview dragging | `false` |
| wallpaper | int | `0` global only, `1` per-workspace only, `2` both | `0` |
| wallpaper_layer | string | `auto`, `background`, or `bottom` | `auto` |
| blur | bool | blur the main overview wallpaper | `false` |
| highlight_selected | bool | wallpaper + shadow only on the selected workspace; empties as outline + plus | `false` |
| highlight_padding | int | how far the highlight reaches past the work area | `0` |
| empty_marker | string | when empties show outline: `always`, `drag`, `hover` | `hover` |
| empty_color | color | outline and plus colour | `rgba(255,255,255,0.35)` |
| empty_halo_color | color | thin halo under outline/plus | `rgba(0,0,0,0.4)` |
| empty_color_from_border | bool | use `general:col.active_border` with `empty_color`'s alpha | `false` |
| zoom_step | float | continuous zoom levels per wheel notch (0 desktop → 1 centred → 2 whole grid) | `0.25` |
| zoom_follow_mouse | bool | zooming in selects the workspace under the cursor | `true` |
| window_icons | bool | app icon on a plate in the middle of every window | `true` |
| window_chrome | bool | 2×3 action dock under focused/hovered window (float / maximize / fullscreen · group / pin / close; also on pin-rail icons) | `true` |
| window_icon_size | int | icon size in layout px | `64` |
| window_icon_plate_color | color | plate behind the icon | `rgba(0,0,0,0.45)` |
| icon_theme | string | icon theme before hicolor; empty = hicolor only | empty |
| special_tray | bool | special workspaces as cards at a screen edge | `true` |
| special_tray_edge | string | `right`, `left`, or `bottom` | `right` |
| special_tray_icons | bool | app icons on card bottoms | `true` |
| special_tray_blur | bool | blur behind the tray dock | `true` |
| special_tray_icon_size | int | tray icon size in layout px | `36` |
| special_tray_autohide | bool | hide until the pointer touches the tray edge | `true` |
| special_workspaces | string | seeds the persistent tray roster once | `special` |
| special_tray_auto_name | string | name on +: `off`, `category`, or `app` | `off` |
| special_tray_width | int | card width in layout px | `300` |
| transition_bezier | string | bezier for moving between workspaces; empty = `windowsMove` | empty |
| transition_speed | float | speed for `transition_bezier` | `4` |
| view_switch_bezier | string | bezier for centred ↔ whole-grid; empty = built-in | empty |
| view_switch_speed | float | speed for `view_switch_bezier` | `3.5` |
| exec_on_open / exec_on_close | string | shell command when the overview opens / starts closing | empty |
| input | structure | input overrides inside the overview | see below |
| shadow | structure | workspace-card shadows | see below |

Mouse wheel in the overview steps along the grid axes (`SHIFT` or the horizontal wheel for the other axis). Middle mouse pans; middle click without moving closes the window under the pointer (also in the tray).

### Subcategory `input`

| property | type | description | default |
| --- | --- | --- | --- |
| left_handed | int | swap left/right mouse in overview: `0` off, `1` on, `2` follow `input.left_handed` | `2` |
| scroll_event_delay | number | ms delay between discrete scroll steps | `200` |
| touchpad_scroll_factor | float | touchpad scroll distance multiplier | `1` |
| drag_mode | int | `0` main button drags windows / middle pans, `1` swapped | `0` |
| drag_threshold | int | px before press becomes drag/pan; `0` disables | `10` |

### Subcategory `shadow`

| property | type | description | default |
| --- | --- | --- | --- |
| enabled | bool | draw a shadow around each workspace card | `false` |
| range | int | shadow range in layout px | `decoration.shadow.range` |
| render_power | int | shadow falloff power | `decoration.shadow.render_power` |
| color | color or gradient | shadow color | `decoration.shadow.color` |
| active_color | color or gradient | shadow of the selected workspace | unset |

## Gestures

```lua
hl.plugin.gridoverview.gesture({ fingers = 3, direction = "vertical" })
hl.plugin.gridoverview.gesture({ fingers = 4, direction = "vertical", mod = "SUPER", scale = 1.5 })
hl.plugin.gridoverview.gesture({ fingers = 3, direction = "vertical", action = "unset" })
```

| field | type | description | default |
| --- | --- | --- | --- |
| fingers | number | finger count (2–9) | required |
| direction | string | swipe direction (`up`, `down`, `left`, `right`, `vertical`, …) | required |
| action | string | `overview` to register, `unset` to remove | `overview` |
| mod | string | modifier held during the gesture | none |
| scale | number | gesture delta scale, [0.1–10] | `1.0` |
| disable_inhibit | bool | fire even when an app inhibits gestures | `false` |

[Back to Configuration](Configuration.md)
