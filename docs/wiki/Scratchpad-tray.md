With `special_tray = true` (default), Hyprland special workspaces appear as live cards at a screen edge (`special_tray_edge`, default `right`). By default the tray auto-hides until the pointer touches that edge (`special_tray_autohide = true`).

## Using the tray

- Drag a window onto a card to move it into that scratchpad; drag out onto any workspace to restore it.
- Click a card to show that scratchpad.
- App icons on the card (when `special_tray_icons` is on): drag to move that app's topmost window; click to show the scratchpad with that window focused.
- Pencil renames the card (centred field; Enter / Escape). Anonymous cards start as `scratch`, `scratch-2`, … with no name in the pill.
- × closes the card's windows and removes it from the persistent roster.
- Grip (or Alt+drag) reorders cards; order is stored in state.
- Drop on **+** creates a new card. `special_tray_auto_name` controls the name: `off` (default, anonymous), `category`, or `app`.
- A dragged window shrinks toward card size as it approaches the tray. At the tray edge the tray takes priority; pushing further to the screen edge scrolls the grid behind it.
- Middle click without moving closes the window under the pointer, including in the tray.

## Persistence

`special_workspaces` only **seeds** the roster once. Afterwards × / + / rename edit `~/.local/state/gridoverview/special_workspaces`. `special` is Hyprland's unnamed scratchpad.

## Useful options

| option | default | notes |
| --- | --- | --- |
| `special_tray` | `true` | master switch |
| `special_tray_edge` | `right` | screen edge: `right`, `left`, or `bottom` |
| `special_tray_width` | `300` | card width; cards shrink if they do not fit |
| `special_tray_autohide` | `true` | hide until the pointer touches the tray edge |
| `special_tray_blur` | `true` | blur behind the dock (needs Hyprland blur) |
| `special_tray_icons` | `true` | app icons on card bottoms |
| `special_tray_icon_size` | `36` | icon size in layout px |

For scripts, `hl.plugin.gridoverview.tray_layout()` returns the tray under the pointer as JSON (cards, add button, icons, chrome hitboxes).
