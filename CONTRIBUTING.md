# Contributing

Contributions are welcome — fixes, cleanup, and small improvements. Open a pull request; it will be reviewed when there is time.

This project is a **workspace grid overview** for Hyprland (free grid, scratchpad tray, mouse-first). Please keep changes aligned with that: building up on the defaults, not requiring config for core behaviour. Larger feature ideas are best discussed first.

## Bug reports and fixes

Bug fixes take priority over new functionality.

If you find a bug, try to narrow down which Hyprland or plugin option exposes it. That makes reproduction much easier — most issues show up only with specific configs.

## Development notes

Useful test coverage includes multi-monitor setups, high refresh rates, and the scratchpad tray together with the free grid. Trackpad gestures are inherited and not dogfooded by the maintainers — hardware-tested fixes or confirmation that they still work are especially welcome.

The in-session check is `python3 scripts/selftest.py` (needs write access to `/dev/uinput`).

GitHub Actions builds the plugin with Nix against Hyprland **v0.57.2** on every relevant push/PR. A non-blocking smoke build against Hyprland `main` runs on the weekly schedule / manual dispatch.
