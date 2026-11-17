# Contributing

Contributions are welcome — fixes, cleanup, and small improvements. Open a pull request; it will be reviewed when there is time.

This project is a **workspace grid overview** for Hyprland (free grid, scratchpad tray, mouse-first). Please keep changes aligned with that: building up on the defaults, not requiring config for core behaviour. Larger feature ideas are best discussed first.

## Bug reports and fixes

Bug fixes take priority over new functionality.

If you find a bug, try to narrow down which Hyprland or plugin option exposes it. That makes reproduction much easier — most issues show up only with specific configs.

## Development notes

Useful test coverage includes multi-monitor setups, high refresh rates, and the scratchpad tray together with the free grid. Gesture input help is welcome; a PR with a working implementation is more useful than a feature request alone.

The in-session check is `python3 scripts/selftest.py` (needs write access to `/dev/uinput`).
