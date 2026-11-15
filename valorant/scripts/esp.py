"""
Valorant ESP - radar overlay.

Nothing is injected. The window is transparent and click-through and every
number comes from the driver via reader.py.

Offsets in use: actor array static 0xD81F3B0, position root+0x170 as doubles.
The pawn field is unreliable on this build, so the radar centres on you when
your position reads cleanly and on the player centroid otherwise.

    python esp.py                 # 80 m
    python esp.py --range 15000   # wider
    python esp.py --boxes         # boxes instead, if a camera resolves

F9 pauses, END quits. Game must be windowed or borderless.
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wt
import math
import sys
import tkinter as tk
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import reader  # noqa: E402

GWL_EXSTYLE = -20
WS_EX_LAYERED = 0x00080000
WS_EX_TRANSPARENT = 0x00000020
WS_EX_NOACTIVATE = 0x08000000
WS_EX_TOOLWINDOW = 0x00000080

KEY = "#010203"
ENEMY = "#ff4d4d"
SELF = "#ffffff"
DIM = "#5a6472"
RING = "#333a45"

SIZE = 460
MARGIN = 18
RADIUS = SIZE / 2 - 26

_user32 = ctypes.windll.user32
_user32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
_user32.EnumWindows.argtypes = [ctypes.c_void_p, wt.LPARAM]

_ENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def health_colour(fraction: float) -> str:
    fraction = max(0.0, min(1.0, fraction))
    return f"#{int(255 * (1 - fraction)):02x}{int(255 * fraction):02x}40"


class Esp:
    def __init__(self, options):
        self.options = options
        self.game = None
        self.error = ""
        self.paused = False

        self.root = tk.Tk()
        self.root.overrideredirect(True)
        self.root.attributes("-topmost", True)
        self.root.attributes("-transparentcolor", KEY)
        self.root.configure(bg=KEY)

        self.canvas = tk.Canvas(self.root, bg=KEY, highlightthickness=0, bd=0)
        self.canvas.pack(fill="both", expand=True)
        self.root.update_idletasks()

        hwnd = wt.HWND(int(self.root.wm_frame(), 16))
        style = _user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
        _user32.SetWindowLongW(
            hwnd, GWL_EXSTYLE,
            style | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        )

        self.root.bind_all("<F9>", self.toggle)
        self.root.bind_all("<End>", lambda _e: self.root.destroy())

    def toggle(self, _event=None):
        self.paused = not self.paused

    def hud(self, text):
        self.canvas.create_text(10, 10, anchor="nw", text=text, fill="#dfe6ef", font=("Consolas", 9))

    def connect(self):
        if self.game is not None:
            return True
        try:
            game = reader.Game(allow_stale=self.options.allow_stale)
            game.open()
        except Exception as error:
            self.error = str(error)
            return False
        self.game = game
        return True

    def frame(self):
        if not self.paused:
            try:
                self.render()
            except Exception as error:
                self.error = str(error)
        self.root.after(max(1, 1000 // self.options.fps), self.frame)

    def render(self):
        self.canvas.delete("all")

        if not self.connect():
            self.hud(f"not ready: {self.error}")
            return

        self.root.geometry(f"{SIZE}x{SIZE}+{MARGIN}+{MARGIN}")

        origin, local = self.game.centre()
        if origin is None:
            self.hud("no actors - be in a match")
            return

        actors = list(self.game.actors())

        if self.options.boxes:
            camera = self.game.camera()
            if camera is not None:
                self.render_boxes(camera, actors)
                return

        self.render_radar(origin, local, actors)

    def render_radar(self, origin, local, actors):
        centre = SIZE / 2
        scale = RADIUS / self.options.range

        self.canvas.create_oval(
            centre - RADIUS, centre - RADIUS, centre + RADIUS, centre + RADIUS,
            outline=DIM, width=1,
        )
        for step in (0.33, 0.66):
            r = RADIUS * step
            self.canvas.create_oval(centre - r, centre - r, centre + r, centre + r, outline=RING)
        self.canvas.create_oval(centre - 2, centre - 2, centre + 2, centre + 2, fill=SELF, outline="")

        shown = 0

        for actor in actors:
            dx = actor.position[0] - origin[0]
            dy = actor.position[1] - origin[1]
            distance = math.hypot(dx, dy)
            if distance > self.options.range or actor.address == local:
                continue

            x = centre + dx * scale
            y = centre - dy * scale

            self.canvas.create_oval(x - 3, y - 3, x + 3, y + 3, fill=ENEMY, outline="")
            self.canvas.create_text(
                x, y - 9, text=f"{distance / 100:.0f}m", fill=ENEMY, font=("Consolas", 8)
            )
            if actor.max_health > 0:
                self.canvas.create_rectangle(
                    x - 9, y + 6, x + 9, y + 8,
                    fill=health_colour(actor.health / actor.max_health), outline="",
                )
            shown += 1

        mode = "self" if local else "centroid"
        self.hud(
            f"radar {shown} / {len(actors)}  {self.options.range / 100:.0f}m  "
            f"origin {mode}  build {self.game.patch or '?'}  F9 pause"
        )

    def render_boxes(self, camera, actors):
        location, rotation, fov = camera
        width, height = 1920, 1080

        for actor in actors:
            feet = reader.world_to_screen(location, rotation, fov, actor.position, width, height)
            head = reader.world_to_screen(
                location, rotation, fov,
                (actor.position[0], actor.position[1], actor.position[2] + actor.height * 2),
                width, height,
            )
            if feet is None or head is None:
                continue

            span = max(8.0, (feet[1] - head[1]) * 0.42)
            self.canvas.create_rectangle(
                feet[0] - span / 2, head[1], feet[0] + span / 2, feet[1],
                outline=ENEMY, width=2,
            )

        self.hud(f"boxes  fov {fov:.0f}  F9 pause")


def main(argv):
    parser = argparse.ArgumentParser(description="Valorant ESP")
    parser.add_argument("--range", type=float, default=8000.0, help="radar radius in world units")
    parser.add_argument("--fps", type=int, default=12)
    parser.add_argument("--boxes", action="store_true")
    parser.add_argument("--allow-stale", action="store_true")
    options = parser.parse_args(argv)

    esp = Esp(options)
    esp.root.after(0, esp.frame)
    esp.root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
