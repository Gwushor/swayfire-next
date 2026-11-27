# Swayfire-Next

Sway-style tiling plugin for the Wayfire compositor with Hyprland-style
atomic fullscreen handling. Written to replace `simple-tile`, which
destabilizes fullscreen games (Hunt: Showdown, Proton/Wine titles).

## What it does

- **Sway/i3-style container tree**: `splith`/`splitv` splits with proportional
  resizing when windows open/close (`split_node_t` in `src/tree.cpp`).
- **Hyprland-style fullscreen**: fullscreen requests are serviced by the plugin
  itself (signal `view_fullscreen_request_signal` -> `carried_out`), so a
  fullscreen view takes the whole workarea while the tree keeps its layout —
  no tile-exit flapping when Steam helper windows appear mid-launch.
- **Directional focus**: `find_first_view_in_direction` walks the tree
  geometry, Sway neighbor semantics.
- Views matching `swayfire-next/tile_by_default` are tiled on map; everything
  else floats by default.

## Install

```bash
meson setup build && ninja -C build
sudo cp build/libswayfire-next.so /usr/lib/wayfire/
sudo cp metadata/swayfire-next.xml /usr/share/wayfire/metadata/
```

(`meson install` needs sudo; the pip-installed meson loses its module under
`sudo` — plain `cp` is the reliable path.)

Then enable it in `~/.config/wayfire/user.ini`:

```ini
[core]
plugins = ... swayfire-next
```

(Keep `simple-tile` out of that list — two tiling plugins fight each other.)
Reload: `ryoku wm act config.reload`, or re-login.

## Keybindings

Default bindings live in `metadata/swayfire-next.xml`; override in
`~/.config/wayfire/user.ini`:

```ini
[swayfire-next]
tile_by_default = type is "toplevel"
key_focus_left = <super> <shift> KEY_H
key_focus_right = <super> <shift> KEY_L
key_focus_up = <super> <shift> KEY_K
key_focus_down = <super> <shift> KEY_J
key_toggle_fullscreen = <super> KEY_F
key_toggle_tiled = <super> <shift> KEY_SPACE
```

| Action | Default |
|---|---|
| Focus left / right / up / down | `Super+Shift+H/L/K/J` |
| Toggle fullscreen (focused tiled view) | `Super+F` |
| Toggle tiled/floating (focused view) | `Super+Shift+Space` |

## Behavior notes

- **Fullscreen** (`Super+F`, or the app's own request): the view spans the
  workarea; the rest of the tree is untouched. Focus moves away while
  fullscreen -> fullscreen follows the new focus (`focus_adjacent` in
  `src/main.cpp`).
- **Minimized** views leave the tree; restoring them does not auto-tile yet.
- **Fixed-size views** (dialogs: min size == max size) and child dialogs are
  never tiled.
- Views that map already-fullscreen skip the tree entirely.

## Code map

- `src/tree.hpp` / `src/tree.cpp` — `split_node_t` / `view_node_t`,
  proportional layout, `find_first_view_in_direction`, `flatten_tree`,
  `wset_tree_data_t` (one root per workspace set, autocommit transactions).
- `src/main.cpp` — per-output engine (`swayfire_output_t`), signals
  (map/unmap, fullscreen/tile requests, minimized), keybindings.
- `metadata/swayfire-next.xml` — config schema with defaults.

Builds against Wayfire 0.11 (`wf::txn::transaction_t::create()` +
`tx_manager->schedule_transaction` API).
