Contexts are **opt-in**. By default the grid is free: rows and columns appear when you use them, labels stay off, and you do not need a `contexts` table.

Turn contexts on when you want structural rows (wallpapers, optional monitor pinning) and/or name ··· pencil labels next to the first workspace of each row (or column).

## Labels (rename in the overview)

Set `context_labels = true`. Each context's first workspace shows a pill; the pencil opens a centred rename field (Enter confirms, Escape cancels). Names are stored in `~/.local/state/gridoverview/context_names` and survive restarts. Config does **not** hold the display names.

Clearing a label back to empty restores the default unnamed look.

## Structural slots

With the starter module, set `useContexts = true` and fill `contexts` with wallpaper paths and optional `monitor` pins — no names:

```lua
contexts = {
    { wallpaper = "abstract/waves.jpg" },
    { wallpaper = "nature/forest.jpg", monitor = "HDMI-A-1" },
},
```

Plugin-side companions:

| option | meaning |
| --- | --- |
| `grid_all_contexts` | keep the first workspace of each reserved slot visible even while empty |
| `context_count` | how many slots were reserved (the starter sets this from `#contexts`) |
| `context_label_size` / `context_label_background` / `context_label_color` | look of the pills |

`grid_orientation = "columns"` lays contexts out as columns instead of rows; keys and overview follow.

Wallpaper switching on context change is handled by the starter module (`contextWallpapers`, `wallpaperCommand`), not by the plugin itself.
