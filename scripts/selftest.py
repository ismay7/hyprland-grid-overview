#!/usr/bin/env python3
"""Self-test for gridoverview in a running Hyprland session (Lua config).

Drives a virtual mouse and keyboard (python-evdev, needs write access to /dev/uinput) and checks the results
through hyprctl. It works on its own test window on a free workspace (--workspace, default 91) and leaves your
windows alone; at the end the test window is closed and you are back on the workspace you started from.

    python3 scripts/selftest.py            # run all tests
    python3 scripts/selftest.py --list     # show the tests

Exit code 0 when everything passed. Keys of the example Lua module (GridOverview.step / cycleWorkspace) are only
tested when the module is loaded.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time

TEST_CLASS = "gridoverview-selftest"
TEST_SPECIAL = "special:" + TEST_CLASS


def sh(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True).stdout.strip()


def dispatch(expr):
    return sh(f"hyprctl dispatch '{expr}'")


def lua_value(expr):
    """Evaluate a Lua expression in Hyprland and return it as a string (hyprctl eval does not print results)."""
    path = f"/tmp/gridoverview-selftest-{os.getpid()}.out"
    sh(f"hyprctl eval 'local f = io.open(\"{path}\", \"w\"); f:write(tostring({expr})); f:close()'")
    try:
        with open(path) as f:
            return f.read()
    except OSError:
        return ""
    finally:
        if os.path.exists(path):
            os.remove(path)


def monitor():
    return next(m for m in json.loads(sh("hyprctl monitors -j")) if m["focused"])


def active_workspace():
    return json.loads(sh("hyprctl activeworkspace -j"))["name"]


def special_open():
    return monitor()["specialWorkspace"]["name"] or ""


def clients():
    return json.loads(sh("hyprctl clients -j"))


def test_window():
    return next((c for c in clients() if c["class"] == TEST_CLASS), None)


def user_windows():
    return sorted((c["class"], c["workspace"]["name"], tuple(c["at"]), tuple(c["size"])) for c in clients() if c["class"] != TEST_CLASS)


def workspaces():
    return [w["name"] for w in json.loads(sh("hyprctl workspaces -j"))]


def wait_for(predicate, timeout=3.0, step=0.1):
    end = time.time() + timeout
    while time.time() < end:
        value = predicate()
        if value:
            return value
        time.sleep(step)
    return predicate()


class Input:
    def __init__(self):
        from evdev import UInput, ecodes as e

        self.e = e
        keys = [e.BTN_LEFT, e.BTN_RIGHT, e.BTN_MIDDLE, e.KEY_ESC]
        self.ui = UInput({e.EV_KEY: keys, e.EV_REL: [e.REL_X, e.REL_Y]}, name="gridoverview-selftest")
        time.sleep(1.0)

    def close(self):
        self.ui.close()

    def warp(self, x, y):
        dispatch(f"hl.dsp.cursor.move({{ x = {int(x)}, y = {int(y)} }})")
        time.sleep(0.03)

    def nudge(self):
        self.ui.write(self.e.EV_REL, self.e.REL_Y, 1)
        self.ui.syn()
        time.sleep(0.05)

    def rel_x(self, total, steps=60):
        for _ in range(steps):
            self.ui.write(self.e.EV_REL, self.e.REL_X, total // steps)
            self.ui.syn()
            time.sleep(0.006)

    def button(self, down):
        self.ui.write(self.e.EV_KEY, self.e.BTN_LEFT, 1 if down else 0)
        self.ui.syn()
        time.sleep(0.08)

    def click(self):
        self.button(True)
        self.button(False)

    def middle(self, down):
        self.ui.write(self.e.EV_KEY, self.e.BTN_MIDDLE, 1 if down else 0)
        self.ui.syn()
        time.sleep(0.08)

    def middle_click(self):
        for value in (1, 0):
            self.ui.write(self.e.EV_KEY, self.e.BTN_MIDDLE, value)
            self.ui.syn()
            time.sleep(0.08)

    def escape(self):
        self.ui.write(self.e.EV_KEY, self.e.KEY_ESC, 1)
        self.ui.syn()
        self.ui.write(self.e.EV_KEY, self.e.KEY_ESC, 0)
        self.ui.syn()
        time.sleep(0.2)

    def glide(self, x0, y0, x1, y1, steps=30):
        for i in range(1, steps + 1):
            self.warp(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps)


class SelfTest:
    def __init__(self, args):
        self.args = args
        self.results = []
        self.input = None

    # helpers ----------------------------------------------------------------------------------------------
    def check(self, name, ok, detail=""):
        self.results.append((name, ok))
        print(("PASS " if ok else "FAIL ") + name + (f"  ({detail})" if detail else ""), flush=True)
        return ok

    def overview(self, action):
        dispatch(f'hl.plugin.gridoverview.overview("{action}")')

    def tray(self):
        try:
            return json.loads(lua_value("hl.plugin.gridoverview.tray_layout()") or "{}")
        except json.JSONDecodeError:
            return {}

    def monitor_center(self):
        m = monitor()
        scale = m["scale"]
        return m["x"] + m["width"] / scale / 2, m["y"] + m["height"] / scale / 2, m

    def open_overview(self):
        x, y, _ = self.monitor_center()
        self.input.warp(x, y)
        self.input.nudge()
        self.overview("toggle")
        time.sleep(1.4)

    def close_overview(self):
        if self.tray().get("open"):
            self.overview("toggle")
            time.sleep(1.0)

    def reveal_tray(self):
        layout = self.tray()
        if layout.get("shown"):
            return layout
        self.input.rel_x(4000)
        return wait_for(lambda: (lambda t: t if t.get("shown") else None)(self.tray()), timeout=2.0) or self.tray()

    # setup ------------------------------------------------------------------------------------------------
    def preflight(self):
        problems = []
        if not shutil.which("hyprctl"):
            problems.append("hyprctl not found")
        if "gridoverview" not in sh("hyprctl plugin list"):
            problems.append("gridoverview plugin not loaded")
        if lua_value("hl.plugin.gridoverview.tray_layout ~= nil") != "true":
            problems.append("plugin too old (no tray_layout), rebuild/reload it")
        try:
            import evdev  # noqa: F401
        except ImportError:
            problems.append("python-evdev missing")
        if not os.access("/dev/uinput", os.W_OK):
            problems.append("no write access to /dev/uinput")
        if not self.args.terminal:
            problems.append("no terminal for the test window (kitty, foot or alacritty), see --terminal")
        for problem in problems:
            print("SKIP " + problem)
        return not problems

    def setup(self):
        self.start_workspace = active_workspace()
        self.before = user_windows()
        if self.start_workspace != str(self.args.workspace):
            dispatch(f"hl.dsp.focus({{ workspace = {self.args.workspace} }})")
            time.sleep(0.6)
        command = {"kitty": f"kitty --class {TEST_CLASS}", "foot": f"foot --app-id {TEST_CLASS}",
                   "alacritty": f"alacritty --class {TEST_CLASS}"}[os.path.basename(self.args.terminal)]
        dispatch(f'hl.dsp.exec_cmd("{command}")')
        window = wait_for(test_window, timeout=6.0)
        if not window:
            return False
        time.sleep(0.5)
        return True

    def teardown(self):
        self.close_overview()
        if special_open():
            dispatch(f'hl.dsp.workspace.toggle_special("{special_open()[8:]}")')
            time.sleep(0.6)
        window = test_window()
        if window:
            dispatch(f'hl.dsp.window.close({{ window = "address:{window["address"]}" }})')
            wait_for(lambda: test_window() is None, timeout=3.0)
        if active_workspace() != self.start_workspace:
            dispatch(f"hl.dsp.focus({{ workspace = {self.start_workspace} }})")
            time.sleep(0.6)
        self.check("your windows are unchanged", user_windows() == self.before)
        self.check("back on the start workspace", active_workspace() == self.start_workspace, active_workspace())

    # tests ------------------------------------------------------------------------------------------------
    def test_keys(self):
        if lua_value("GridOverview ~= nil") != "true":
            print("SKIP keys (example Lua module not loaded)")
            return
        ws = self.args.workspace
        row, col = divmod(ws, 10)
        seq = []
        for d_row, d_col in ((0, 1), (0, -1), (-1, 0), (1, 0)):
            sh(f"hyprctl eval 'GridOverview.step({d_row}, {d_col})'")
            time.sleep(0.6)
            seq.append(active_workspace())
        expected = [str(row * 10 + col + 1), str(ws), str((row - 1) * 10 + col), str(ws)]
        self.check("keys: step right/left/up/down", seq == expected, " ".join(seq))
        sh("hyprctl eval 'GridOverview.cycleWorkspace(1)'")
        time.sleep(0.6)
        forward = active_workspace()
        sh("hyprctl eval 'GridOverview.cycleWorkspace(-1)'")
        time.sleep(0.6)
        self.check("keys: cycle forward/back", forward == str(ws + 1) and active_workspace() == str(ws), f"{forward} {active_workspace()}")

    def test_views(self):
        self.open_overview()
        for action in ("fit", "center", "zoomout", "zoomin"):
            self.overview(action)
            time.sleep(0.9)
        alive = sh("hyprctl version").startswith("Hyprland")
        self.close_overview()
        self.check("views and zoom (fit, center, zoomout, zoomin)", alive)

    def test_drag_cancel(self):
        x, y, m = self.monitor_center()
        self.open_overview()
        self.input.warp(x, y)
        self.input.nudge()
        self.input.button(True)
        self.input.glide(x, y, m["x"] + m["width"] / m["scale"] - 60, y)
        time.sleep(0.6)
        self.input.escape()
        self.input.button(False)
        time.sleep(0.8)
        window = test_window()
        self.check("cancelled drag leaves the window in place", bool(window) and window["workspace"]["name"] == str(self.args.workspace))
        self.close_overview()
        if active_workspace() != str(self.args.workspace):
            dispatch(f"hl.dsp.focus({{ workspace = {self.args.workspace} }})")
            time.sleep(0.6)

    def test_tray(self):
        self.open_overview()
        layout = self.tray()
        if not layout.get("cards"):
            self.close_overview()
            print("SKIP tray (special_tray off or no cards)")
            return
        layout = self.reveal_tray()
        self.check("tray shows at the right edge", bool(layout.get("shown")))

        # drag the test window onto "+": a new scratchpad named after its app
        x, y, _ = self.monitor_center()
        self.input.warp(x, y)
        self.input.nudge()
        self.input.button(True)
        add = layout["add"]
        ax, ay = add["x"] + add["w"] / 2, add["y"] + add["h"] / 2
        edge_x = ax + add["w"] / 2 + 10
        self.input.glide(x, y, edge_x, y)
        time.sleep(0.7)
        layout = self.tray()
        add = layout.get("add") or add
        ax, ay = add["x"] + add["w"] / 2, add["y"] + add["h"] / 2
        self.input.glide(edge_x, y, ax, ay, steps=20)
        time.sleep(0.4)
        self.input.button(False)
        window = wait_for(lambda: (lambda w: w if w and w["workspace"]["name"] == TEST_SPECIAL else None)(test_window()), timeout=2.0)
        self.check("drop on + creates a scratchpad named after the app", bool(window), (test_window() or {}).get("workspace", {}).get("name", "-"))
        self.close_overview()
        if window:
            self.test_tray_icon(window["address"])

        # back to its workspace: the emptied scratchpad disappears
        window = test_window()
        if window and window["workspace"]["name"] == TEST_SPECIAL:
            dispatch(f'hl.dsp.window.move({{ workspace = {self.args.workspace}, follow = false, window = "address:{window["address"]}" }})')
            time.sleep(0.8)
        self.check("emptied scratchpad disappears", TEST_SPECIAL not in workspaces())

        # reorder by the grip on the name pill, then put the order back
        if active_workspace() != str(self.args.workspace):
            dispatch(f"hl.dsp.focus({{ workspace = {self.args.workspace} }})")
            time.sleep(0.6)
        self.open_overview()
        layout = self.reveal_tray()
        cards = layout.get("cards") or []
        if len(cards) >= 2 and cards[0].get("grip"):
            before = [c["name"] for c in cards]
            grip = cards[0]["grip"]
            gx, gy = grip["x"] + grip["w"] / 2, grip["y"] + grip["h"] / 2
            target = cards[1]
            ty = target["y"] + target["h"] + 20
            self.input.warp(gx, gy)
            self.input.nudge()
            time.sleep(0.2)
            self.input.button(True)
            self.input.glide(gx, gy, gx, ty)
            time.sleep(0.3)
            self.input.button(False)
            time.sleep(0.6)
            after = [c["name"] for c in (self.tray().get("cards") or [])]
            moved = after == before[1:2] + before[:1] + before[2:] or (after and after[0] != before[0])
            self.check("grip drag reorders scratchpad cards", bool(moved), f"{before} -> {after}")
            # restore: drag the moved card back up by its grip
            layout = self.tray()
            cards = layout.get("cards") or []
            if cards and cards[0].get("grip") and before:
                # find the card that should be first again
                idx = next((i for i, c in enumerate(cards) if c["name"] == before[0]), None)
                if idx is not None:
                    grip = cards[idx]["grip"]
                    gx, gy = grip["x"] + grip["w"] / 2, grip["y"] + grip["h"] / 2
                    top = cards[0]["y"] - 20
                    self.input.warp(gx, gy)
                    self.input.nudge()
                    self.input.button(True)
                    self.input.glide(gx, gy, gx, top)
                    time.sleep(0.3)
                    self.input.button(False)
                    time.sleep(0.5)
            restored = [c["name"] for c in (self.tray().get("cards") or [])]
            self.check("tray order restored after test", restored == before, f"{restored}")
        else:
            print("SKIP tray reorder (need ≥2 cards with grip)")
        self.close_overview()

        # a click on the first card shows that scratchpad
        if active_workspace() != str(self.args.workspace):
            dispatch(f"hl.dsp.focus({{ workspace = {self.args.workspace} }})")
            time.sleep(0.6)
        self.open_overview()
        layout = self.reveal_tray()
        card = layout["cards"][0]
        self.input.warp(card["x"] + card["w"] / 2, card["y"] + card["h"] / 2)
        self.input.nudge()
        time.sleep(0.3)
        self.input.click()
        shown = wait_for(lambda: special_open() == "special:" + card["name"], timeout=2.5)
        self.check(f"click on a card shows its scratchpad ({card['name']})", bool(shown), special_open() or "-")
        if special_open():
            dispatch(f'hl.dsp.workspace.toggle_special("{special_open()[8:]}")')
            time.sleep(0.6)

    def tray_icon(self, address):
        layout = self.reveal_tray()
        for card in layout.get("cards", []):
            for icon in card.get("icons", []):
                if icon["window"] == address:
                    return icon
        return None

    def test_tray_icon(self, address):
        """The test window sits alone in its scratchpad: its icon opens it focused, a drag takes it out."""
        if "true" not in sh("hyprctl getoption plugin:gridoverview:special_tray_icons"):
            print("SKIP tray icons (special_tray_icons off)")
            return

        self.open_overview()
        icon = self.tray_icon(address)
        if not self.check("tray shows an icon for the window", bool(icon)):
            self.close_overview()
            return
        self.input.warp(icon["x"] + icon["w"] / 2, icon["y"] + icon["h"] * 0.75)
        self.input.nudge()
        time.sleep(0.3)
        self.input.click()
        shown = wait_for(lambda: special_open() == TEST_SPECIAL, timeout=2.5)
        focused = wait_for(lambda: json.loads(sh("hyprctl activewindow -j") or "{}").get("address") == address, timeout=1.5)
        self.check("click on an icon shows its scratchpad, window focused", bool(shown and focused), special_open() or "-")
        if special_open():
            dispatch(f'hl.dsp.workspace.toggle_special("{special_open()[8:]}")')
            time.sleep(0.6)

        self.open_overview()
        icon = self.tray_icon(address)
        if not icon:
            self.close_overview()
            return
        ix, iy = icon["x"] + icon["w"] / 2, icon["y"] + icon["h"] * 0.75
        x, y, _ = self.monitor_center()
        self.input.warp(ix, iy)
        self.input.nudge()
        time.sleep(0.3)
        self.input.button(True)
        self.input.glide(ix, iy, x, y)
        time.sleep(0.4)
        self.input.button(False)
        moved = wait_for(lambda: (lambda w: w if w and w["workspace"]["name"] != TEST_SPECIAL else None)(test_window()), timeout=2.0)
        self.check("drag on an icon takes the window out", bool(moved), (test_window() or {}).get("workspace", {}).get("name", "-"))
        self.close_overview()
        window = test_window()
        if window and window["workspace"]["name"] != str(self.args.workspace):
            dispatch(f'hl.dsp.window.move({{ workspace = {self.args.workspace}, follow = false, window = "address:{window["address"]}" }})')
            time.sleep(0.6)

    def test_middle_drag(self):
        self.open_overview()
        x, y, _ = self.monitor_center()
        self.input.warp(x, y)
        self.input.nudge()
        self.input.middle(True)
        self.input.rel_x(400, steps=40)
        self.input.middle(False)
        time.sleep(0.8)
        self.check("middle drag pans without closing the window", test_window() is not None)
        self.close_overview()

    def test_middle_click(self):
        if active_workspace() != str(self.args.workspace):
            dispatch(f"hl.dsp.focus({{ workspace = {self.args.workspace} }})")
            time.sleep(0.6)
        self.open_overview()
        x, y, _ = self.monitor_center()
        self.input.warp(x, y)
        self.input.nudge()
        self.input.middle_click()
        closed = wait_for(lambda: test_window() is None, timeout=3.0)
        self.check("middle click closes the window under the pointer", bool(closed))
        self.close_overview()

    def run(self):
        if not self.preflight():
            return 2
        self.input = Input()
        try:
            if not self.check("test window opens", self.setup()):
                return 1
            self.test_keys()
            self.test_views()
            self.test_drag_cancel()
            self.test_tray()
            self.test_middle_drag()
            self.test_middle_click()
        finally:
            try:
                self.teardown()
            finally:
                self.input.close()
        failed = [name for name, ok in self.results if not ok]
        print(f"\n{len(self.results) - len(failed)}/{len(self.results)} passed" + (f", failed: {', '.join(failed)}" if failed else ""))
        return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--workspace", type=int, default=91, help="free workspace for the test window (default 91)")
    parser.add_argument("--terminal", default=next((t for t in ("kitty", "foot", "alacritty") if shutil.which(t)), None),
                        help="terminal for the test window: kitty, foot or alacritty")
    parser.add_argument("--list", action="store_true", help="list the tests and exit")
    args = parser.parse_args()
    if args.list:
        print("keys (example Lua module), views and zoom, cancelled drag, tray: reveal, drop on +, icon click and drag, "
              "card reorder by grip, emptied scratchpad, card click, middle drag pans, middle click closes a window; at the end: your windows unchanged, back on the start workspace")
        return 0
    if args.workspace < 11 or args.workspace % 10 == 0 or args.workspace // 10 < 2:
        parser.error("--workspace must be a grid workspace below the first row, e.g. 91")
    return SelfTest(args).run()


if __name__ == "__main__":
    sys.exit(main())
