#!/usr/bin/python3
"""Mock game driver: speaks the real `--driver-fd` wire protocol without a game.

The contract suites run against this and against the real binary, so the supervisor can be tested
end to end without a 7 to 10 second, 1 GB boot. It is started exactly like the game (the fd shim
execs it with `--driver-fd N` appended; on Windows the client runs it under `python` with
`--driver-fd 0`, requests on stdin and answers on stdout) and accepts the game's other flags,
including `--driver-deny-list <file>`.

Protocol commands (same as the real driver): `ping`, `state`, `wait`, `move`, `seed`, `set_time`,
`action`,
`key`, `quit`. The mock world is the contract's fixture: the avatar is walled in on every compass
side and cannot go up, so a move is blocked or refused and costs no time; `wait` and a raw `pause`
spend turns; `inventory`, `look` and `map` open menus that a `key` answers.
`view` answers for the same walled-in fixture (the avatar's neighbours are walls, the rest is out
of sight, and a rock lies underfoot), and `attach_view` adds that view to every observation.
`run_scene <name>` reads `<name>.lua` from the `--driver-scenes` directory and runs it the only way a
mock can: it understands `gdebug.log_info("...")`, `print("...")` (a logged line), `error("...")`
(the Scene fails, its error last in the lines) and `return false`, one statement per line, and
nothing else. It answers like the real driver, whose Lua runs everything.

Test hooks (mock only, never part of the protocol): `info` reports the user directory and world the
mock was started with, `dirty` writes a file into that world, `spawn_child` starts a grandchild in
the process group and reports its pid, `hang` never answers, `sleep` answers after `seconds`, `log`
writes `text` (a `LEVEL : text` line) to the debug log, `hurt` takes `amount` off `hp`, `rewind`
moves the turn counter back `turns` turns and `glitch` moves it on by one without saying time
passed (both deliberately wrong, for the oracle that watches the counter), and the raw action
`fidget` is accepted and does nothing (`no_effect`).
`melee`, `fire` and `smash` take a `dir` or a `pos` target and answer as the real driver does in
the walled-in fixture (melee and fire are refused, smash bashes a wall); `hurt` taking `hp` to 0 or
below makes every response carry `outcome: died`.
Env `MOCK_BOOT_DELAY_S` delays the first answer, like a slow boot; `MOCK_TURN_DELAY_MS` makes
every turn a `wait` spends take that long, like a slow world step.
`capture <dir> [mode]` is the windowed mode's command: the mock writes a stand-in frame (a BMP of
twice the window's size for `final`, as on a HiDPI display, a PNG of the window's size for
`state`) and the map snapshot of the turn, named `turn-<turn>-<n>-final.bmp`,
`turn-<turn>-<n>-state.png` and `turn-<turn>-<n>-map.json` (`n` counts the captures so far), and
refuses with `no_drawable` while the window is `minimise`d (a mock
hook, undone by `restore`). A windowless mock answers `capture` with a protocol error.
The frames are real images of synthetic content: the left half is as blue as the render state
says and `render` (mock only) sets it: `state` (a whole number; each step is 100 of blue), `noise`
(how many pixels flip per capture), `stuck` (later `state`s are ignored), `message` (a game
message, answered in `new_messages`), `report_window` (`WxH`, what captures claim the window is) and
`scale` (the final composite is the window times this; 2 by default, as on a Retina display).

Like the game it writes `<userdir>/config/debug.log`, buffered: nothing reaches the file until the
process exits. Each line starts with the game's `HH:MM:SS.mmm` wall-clock stamp. The world save
(the fixture) scripts what is logged, one `LEVEL : text` line per file line, so a fixture decides
its own noise: `mock_log_boot.txt` is logged during boot, before the first ping is answered;
`mock_log_idle.txt` shortly after it is answered; `mock_log_quit.txt` while shutting down after
`quit`. A world with `mock_log_none` writes no debug.log at all.

A world with `mock_diverge` makes the first `wait` report a random `pain`, so two same-seed Episodes
of it disagree there, as the real game's same-seed Episodes sometimes do.

A world with `mock_quit_crash` answers `quit` and then crashes instead of exiting cleanly, as the
real game did on Windows after drawing the overmap: an access violation (0xC0000005) on Windows,
SIGSEGV elsewhere.
"""
from __future__ import annotations

import atexit
import json
import os
import random
import re
import signal
import struct
import subprocess
import sys
import threading
import time
import zlib
from datetime import datetime

START_TURN = 1000
TURN_CAP = 1000
SEED_LIMIT = 2**32
COMPASS = ("n", "ne", "e", "se", "s", "sw", "w", "nw")
VITALS = {"hp": 100, "pain": 0, "stamina": 100, "hunger": 0, "thirst": 0}
MENUS = ("inventory", "look", "map")
# Actions that read input with no modal to answer it: the real game hangs on them, the mock's
# guard reports them as `unsupported` instead, as the real driver's no-fiber guard does.
BLOCKING_READS = ("craft", "drop", "eat", "apply", "wear", "read")
FREE_ACTIONS = ("pause",)
NO_EFFECT_ACTIONS = ("fidget",)
# The deny list a driver started without `--driver-deny-list` uses.
DEFAULT_DENY = BLOCKING_READS
NAMED_KEYS = ("ESC", "ENTER", "SPACE", "TAB", "UP", "DOWN", "LEFT", "RIGHT")

# How far each combat command reaches, in tiles: melee and smash one, fire as far as the map goes.
COMBAT_REACH = {"melee": 1, "fire": 132, "smash": 1}

# The view command: its default radius and the widest it answers.
VIEW_DEFAULT_RADIUS = 5
VIEW_MAX_RADIUS = 10

# The Scenes `run_scene` finds: a name is letters, digits, `_` and `-`, and a Scene is one file.
SCENE_NAME = re.compile(r"^[A-Za-z0-9_-]+$")
SCENE_LOGGED = re.compile(r'^\s*(?:gdebug\.log_(?:info|warn|error)|print)\("(.*)"\)\s*$')
SCENE_RAISES = re.compile(r'^\s*error\("(.*)"\)\s*$')


class Game:
    def __init__(self, deny: dict[str, str]) -> None:
        self.turn = START_TURN
        self.hp = VITALS["hp"]
        self.deny = deny
        self.menu: str | None = None
        # The message log; an identical message in a row merges into one entry with a count.
        self.log: list[list] = []
        # Set from the world: the first world step is nondeterministic (see `mock_diverge`).
        self.diverge = False
        # Radius of the view attached to every observation; 0 attaches none.
        self.attach_radius = 0
        # Mock hook: a minimised window has no drawable.
        self.minimised = False
        # Captures written so far: each is named by its turn and its number.
        self.captures = 0
        # Mock hook (`render`): what the captured frames show.
        self.render_state = 1
        self.render_noise = 0
        self.render_stuck = False
        self.reported_window: tuple[int, int] | None = None
        # The composite is the window times this, as on a HiDPI display (2 on a Retina one).
        self.render_scale = 2

    def say(self, text: str) -> str:
        """Logs a message and returns the log entry as the player sees it."""
        if self.log and self.log[-1][0] == text:
            self.log[-1][1] += 1
        else:
            self.log.append([text, 1])
        text, count = self.log[-1]
        return text if count == 1 else f"{text} x {count}"


def stamp() -> str:
    """The game's debug.log time prefix: local wall clock, to the millisecond."""
    now = datetime.now()
    return f"{now:%H:%M:%S}.{now.microsecond // 1000:03d}"


class DebugLog:
    """The game's debug.log: buffered, so only a process that exits writes it out."""

    IDLE_DELAY_S = 0.15
    QUIT_DELAY_S = 0.03

    def __init__(self, userdir: str, world: str) -> None:
        self.world_dir = os.path.join(userdir, "save", world)
        self.path = os.path.join(userdir, "config", "debug.log")
        self.enabled = bool(userdir) and not os.path.exists(
            os.path.join(self.world_dir, "mock_log_none")
        )
        self.lines: list[str] = []
        self.lock = threading.Lock()
        self.write(": Starting log.")

    def write(self, text: str) -> None:
        with self.lock:
            self.lines.append(f"{stamp()} {text}\n")

    def scripted(self, name: str) -> list[str]:
        try:
            with open(os.path.join(self.world_dir, name)) as f:
                return [line.strip() for line in f if line.strip()]
        except OSError:
            return []

    def log_script(self, name: str) -> None:
        for text in self.scripted(name):
            self.write(text)

    def log_script_later(self, name: str) -> None:
        threading.Timer(self.IDLE_DELAY_S, self.log_script, args=(name,)).start()

    def close(self) -> None:
        self.write(": Log shutdown.")
        if not self.enabled:
            return
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        with open(self.path, "a") as f:
            f.write("\n\n-----------------------------------------\n")
            f.writelines(self.lines)
            f.write("-----------------------------------------\n\n")


def option(argv: list[str], name: str) -> str | None:
    return argv[argv.index(name) + 1] if name in argv else None


def is_int(value: object) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def load_deny_list(path: str | None) -> dict[str, str]:
    if path is None:
        return {action: "reads input with no modal fiber" for action in DEFAULT_DENY}
    with open(path) as f:
        return {entry["action"]: entry.get("why", "") for entry in json.load(f)["deny"]}


def observation(rid: int, game: Game, **fields) -> dict:
    obs = {
        "id": rid,
        "status": "ok",
        "outcome": "completed",
        "boundary": "turn_complete",
        "turn": game.turn,
        "time_passed": False,
        "moved": False,
        "new_messages": [],
        "prompt": None,
        **VITALS,
        "hp": game.hp,
    }
    if game.menu:
        obs.update(outcome="awaiting_input", boundary="needs_input", prompt=game.menu)
    obs.update(fields)
    if game.attach_radius and "grid" not in obs:
        obs["view"] = view_members(game.attach_radius)
    if game.hp <= 0:
        obs.update(outcome="died", boundary="turn_complete", prompt=None)
    return obs


def capture(rid: int, game: Game, req: dict, window: str | None) -> dict:
    """The windowed mode's `capture`: a stand-in frame and the map snapshot of the turn."""
    if window is None:
        return error(rid, "capture needs the windowed mode (start the game with --driver-windowed)")
    directory = req.get("dir")
    if not isinstance(directory, str) or not os.path.isabs(directory):
        return error(rid, "`dir` must be an absolute directory path")
    mode = req.get("mode", "final")
    if mode not in ("final", "state"):
        return error(rid, "`mode` must be `final` or `state`")
    if game.minimised:
        return observation(
            rid, game, outcome="refused", reason="no_drawable",
            detail="the window is hidden or minimised: there is no drawable to capture",
        )
    width, height = (int(n) for n in window.split("x"))
    os.makedirs(directory, exist_ok=True)
    stem = os.path.join(directory, f"turn-{game.turn}-{game.captures + 1}-")
    if mode == "final":
        scaled = (int(width * game.render_scale), int(height * game.render_scale))
        frame, size, label = stem + "final.bmp", scaled, "final composite"
        with open(frame, "wb") as f:
            f.write(bmp_bytes(*size, frame_rows(*size, game)))
    else:
        frame, size, label = stem + "state.png", (width, height), "state view"
        with open(frame, "wb") as f:
            f.write(png_bytes(*size, frame_rows(*size, game)))
    map_path = stem + "map.json"
    with open(map_path, "w") as f:
        json.dump({"frame": game.turn, "turn": game.turn, "z": 0, "player": [60, 60, 0]}, f)
    game.captures += 1
    # Mock hook: the window the game claims to be in, to test a Trial's size against it.
    reported = game.reported_window or (width, height)
    return observation(
        rid, game,
        capture={
            "mode": mode, "label": label, "frame": frame, "map": map_path,
            "width": size[0], "height": size[1],
            "window_width": reported[0], "window_height": reported[1],
        },
    )


def render(rid: int, game: Game, req: dict) -> dict:
    """Mock hook: what the frames show. `state` sets the render state (ignored once the render is
    `stuck`, a toggle that cannot be undone), `noise` how many pixels differ per capture, `message`
    logs a game message (the readiness a Trial waits for), `report_window` is the `WxH` the
    captures claim as the window and `scale` what the final composite is multiplied by."""
    fields = {}
    if "state" in req and not game.render_stuck:
        game.render_state = req["state"]
    if "noise" in req:
        game.render_noise = req["noise"]
    if req.get("stuck"):
        game.render_stuck = True
    if "report_window" in req:
        w, h = (int(n) for n in req["report_window"].split("x"))
        game.reported_window = (w, h)
    if "scale" in req:
        game.render_scale = req["scale"]
    if "message" in req:
        fields["new_messages"] = [game.say(str(req["message"]))]
    return observation(rid, game, **fields)


def frame_rows(width: int, height: int, game: Game) -> list[bytearray]:
    """The synthetic frame of the game's render state, top row first, as rows of R, G, B bytes.

    Red runs left to right and green top to bottom; the left half is as blue as the render state
    says (100 per step), so changing the state changes half the frame by a known amount. Noise
    flips the colour of that many pixels, picked by the number of the capture: two captures of one
    state differ by about twice the noise's pixels, and the same capture number differs the same
    way on every run.
    """
    red = bytes(x * 255 // max(width - 1, 1) for x in range(width))
    shade = min(255, game.render_state * 100)
    blue = bytes(shade if x < width // 2 else 0 for x in range(width))
    rows = []
    for y in range(height):
        row = bytearray(3 * width)
        row[0::3] = red
        row[1::3] = bytes([y * 255 // max(height - 1, 1)]) * width
        row[2::3] = blue
        rows.append(row)
    rng = random.Random(game.captures + 1)
    for _ in range(game.render_noise):
        at = 3 * rng.randrange(width)
        row = rows[rng.randrange(height)]
        for channel in range(3):
            row[at + channel] = (row[at + channel] + 128) % 256
    return rows


def bmp_bytes(width: int, height: int, rows: list[bytearray]) -> bytes:
    """A 24-bit bottom-up BMP, as the game's swapchain dump writes it."""
    stride = (width * 3 + 3) & ~3
    pixels = b"".join(_bgr(row) + b"\0" * (stride - width * 3) for row in reversed(rows))
    return (
        b"BM" + struct.pack("<IHHI", 54 + len(pixels), 0, 0, 54)
        + struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(pixels), 0, 0, 0, 0)
        + pixels
    )


def _bgr(row: bytearray) -> bytes:
    out = bytearray(len(row))
    out[0::3] = row[2::3]
    out[1::3] = row[1::3]
    out[2::3] = row[0::3]
    return bytes(out)


def png_bytes(width: int, height: int, rows: list[bytearray]) -> bytes:
    """An 8-bit RGB PNG with the filter of every row set to none."""

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    scanlines = b"".join(b"\0" + bytes(row) for row in rows)
    return (
        b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(scanlines, 1)) + chunk(b"IEND", b"")
    )


def error(rid: int | None, message: str) -> dict:
    return {"id": rid, "status": "error", "error": message}


def menu_open(rid: int, game: Game) -> dict:
    return error(rid, f"the {game.menu} menu is open: answer it with `key` first")


def view_members(radius: int) -> dict:
    """What the walled-in avatar sees: its neighbours are walls, the rest is out of sight."""
    span = range(-radius, radius + 1)
    grid = [
        "".join("@" if dx == dy == 0 else "#" if abs(dx) <= 1 and abs(dy) <= 1 else "?" for dx in span)
        for dy in span
    ]
    meanings = {"@": "you", "#": "wall", "?": "not in view"}
    used = {symbol for row in grid for symbol in row}
    return {
        "radius": radius,
        "grid": grid,
        "legend": {symbol: meaning for symbol, meaning in meanings.items() if symbol in used},
        "creatures": [],
        "items": [{"id": "4242", "name": "rock", "dx": 0, "dy": 0}],
    }


def view(rid: int, game: Game, req: dict) -> dict:
    radius = req.get("radius", VIEW_DEFAULT_RADIUS)
    if not is_int(radius) or not 1 <= radius <= VIEW_MAX_RADIUS:
        return error(rid, f"`radius` must be a whole number from 1 to {VIEW_MAX_RADIUS}")
    return observation(rid, game, **view_members(radius))


def run_scene(rid: int, game: Game, req: dict, scenes_dir: str) -> dict:
    if game.menu:
        return menu_open(rid, game)
    name = req.get("name")
    if not isinstance(name, str) or not SCENE_NAME.fullmatch(name):
        return error(rid, "`name` must be the name of a Scene: letters, digits, _ and -")
    path = os.path.join(scenes_dir, name + ".lua")
    if not os.path.isfile(path):
        return error(rid, f"unknown scene {name!r}: no {name}.lua in {scenes_dir}")
    lines: list[str] = []
    passed = True
    with open(path) as f:
        for statement in f.read().splitlines():
            if logged := SCENE_LOGGED.match(statement):
                lines.append(logged[1])
            elif raised := SCENE_RAISES.match(statement):
                lines.append(f"error: {path}:1: {raised[1]}")
                passed = False
                break
            elif statement.strip() == "return false":
                passed = False
                break
    return observation(rid, game, scene={"status": "passed" if passed else "failed", "lines": lines})


def attach_view(rid: int, game: Game, req: dict) -> dict:
    radius = req.get("radius")
    if not is_int(radius) or not 0 <= radius <= VIEW_MAX_RADIUS:
        return error(rid, f"`radius` must be a whole number from 0 to {VIEW_MAX_RADIUS}")
    game.attach_radius = radius
    return {"id": rid, "status": "ok", "attach_view": radius}


def wait(rid: int, game: Game, req: dict) -> dict:
    if game.menu:
        return menu_open(rid, game)
    turns = req.get("turns")
    if not is_int(turns) or turns < 1:
        return error(rid, "`turns` must be a positive integer")
    time.sleep(min(turns, TURN_CAP) * float(os.environ.get("MOCK_TURN_DELAY_MS", "0")) / 1000)
    game.turn += min(turns, TURN_CAP)
    if turns > TURN_CAP:
        return observation(rid, game, outcome="interrupted", reason="turn_cap", time_passed=True)
    if game.diverge:
        game.diverge = False
        return observation(rid, game, time_passed=True, pain=random.randrange(1 << 30))
    return observation(rid, game, time_passed=True)


def move(rid: int, game: Game, req: dict) -> dict:
    if game.menu:
        return menu_open(rid, game)
    direction = req.get("dir")
    if direction in COMPASS:
        message = game.say("There is a wall in the way.")
        return observation(rid, game, outcome="blocked", new_messages=[message])
    if direction == "up":
        message = game.say("You can't go up here.")
        return observation(rid, game, outcome="refused", new_messages=[message])
    return error(rid, "`dir` must be a compass direction or `up`")


def seed(rid: int, req: dict) -> dict:
    value = req.get("seed")
    if not is_int(value) or not 0 <= value < SEED_LIMIT:
        return error(rid, "`seed` must be an integer from 0 to 2^32 - 1")
    return {"id": rid, "status": "ok", "seed": value}


DAY = 86400
SEASON_DAYS = 91
YEAR = 4 * SEASON_DAYS * DAY
LATEST_TURN = 2**31 // 2 - 1


def set_time(rid: int, game: Game, req: dict) -> dict:
    """Pins the clock like the real driver: `date` is `YYYY-SS-DD` (year from 1, season 01 to 04,
    day of the season from 01 to 91; the game has no months) and `time` is `HH:MM`."""
    date, clock = req.get("date"), req.get("time")
    if date is None and clock is None:
        return error(rid, "set_time needs a date, a time, or both")
    day_start = game.turn - game.turn % DAY
    if date is not None:
        found = re.fullmatch(r"(\d{4})-(\d{2})-(\d{2})", date) if isinstance(date, str) else None
        if not found:
            return error(rid, f"date must be YYYY-SS-DD; got {date!r}")
        year, season, day = (int(part) for part in found.groups())
        if year < 1 or not 1 <= season <= 4 or not 1 <= day <= SEASON_DAYS:
            return error(rid, f"date must be YYYY-SS-DD; got {date!r}")
        day_start = (year - 1) * YEAR + (season - 1) * SEASON_DAYS * DAY + (day - 1) * DAY
    into_day = game.turn % DAY
    if clock is not None:
        found = re.fullmatch(r"(\d{2}):(\d{2})", clock) if isinstance(clock, str) else None
        if not found or int(found.group(1)) > 23 or int(found.group(2)) > 59:
            return error(rid, f"time must be HH:MM; got {clock!r}")
        into_day = int(found.group(1)) * 3600 + int(found.group(2)) * 60
    target = day_start + into_day
    if target > LATEST_TURN:
        return error(rid, "that date is too far ahead")
    game.turn = target
    return {
        "id": rid,
        "status": "ok",
        "turn": target,
        "date": f"{target // YEAR + 1:04}-{target % YEAR // (SEASON_DAYS * DAY) + 1:02}-"
        f"{target % (SEASON_DAYS * DAY) // DAY + 1:02}",
        "time": f"{target % DAY // 3600:02}:{target % 3600 // 60:02}",
    }


def action(rid: int, game: Game, req: dict) -> dict:
    if game.menu:
        return menu_open(rid, game)
    name = req.get("name")
    if name in game.deny:
        return observation(
            rid, game, outcome="unsupported", reason="deny_list", detail=game.deny[name]
        )
    if name in BLOCKING_READS:
        return observation(rid, game, outcome="unsupported", reason="blocking_read")
    if name in MENUS:
        game.menu = name
        return observation(rid, game)
    if name in FREE_ACTIONS:
        game.turn += 1
        return observation(rid, game, time_passed=True)
    if name in NO_EFFECT_ACTIONS:
        return observation(rid, game, outcome="no_effect")
    return error(rid, f"unknown action {name!r}")


def target_problem(req: dict, kind: str) -> str | None:
    """Why `req` names no usable target, or None when it names one."""
    has_dir, has_pos = "dir" in req, "pos" in req
    if has_dir == has_pos:
        return "name the target with `dir` or `pos`, exactly one of them"
    if has_dir:
        return None if req["dir"] in COMPASS else "`dir` must be a compass direction"
    pos = req["pos"]
    if not isinstance(pos, list) or len(pos) != 2 or not all(is_int(p) for p in pos):
        return "`pos` must be [dx, dy], two whole numbers"
    if pos == [0, 0]:
        return "`pos` is the avatar's own tile: name another tile"
    if max(abs(p) for p in pos) > COMBAT_REACH[kind]:
        return "`pos` is out of reach"
    return None


def combat(rid: int, game: Game, req: dict, kind: str) -> dict:
    """melee, fire and smash: the avatar wields a pocket knife and no creature is adjacent."""
    if game.menu:
        return menu_open(rid, game)
    problem = target_problem(req, kind)
    if problem:
        return error(rid, problem)
    turns = req.get("max_turns")
    if "max_turns" in req and (not is_int(turns) or turns < 1):
        return error(rid, "`max_turns` must be a whole number, at least 1")
    if kind == "melee":
        return observation(rid, game, outcome="refused", detail="There is nothing there to attack.")
    if kind == "fire":
        return observation(rid, game, outcome="refused", detail="Your pocket knife is not a gun.")
    game.turn += 1
    return observation(rid, game, time_passed=True)


def key(rid: int, game: Game, req: dict) -> dict:
    name = req.get("key")
    if not isinstance(name, str) or not (name in NAMED_KEYS or len(name) == 1):
        return error(rid, "`key` must be a key name such as ESC, ENTER or a single character")
    if not game.menu:
        return error(rid, "no menu is open to answer")
    if name == "ESC":
        game.menu = None
    return observation(rid, game)


def crash() -> None:
    """Ends the process the way a crashed game does: an access violation, or SIGSEGV."""
    sys.stdout.flush()
    if os.name == "nt":
        os._exit(-1073741819)  # 0xC0000005 as the signed int _exit takes on Windows
    os.kill(os.getpid(), signal.SIGSEGV)


def main() -> int:
    argv = sys.argv[1:]
    fd = option(argv, "--driver-fd")
    if fd is None:
        print("mock_driver: --driver-fd required", file=sys.stderr)
        return 2
    windowed = option(argv, "--driver-windowed")
    if windowed is not None and not re.fullmatch(r"[1-9][0-9]*x[1-9][0-9]*", windowed):
        # The real game refuses the same: the size is `<width>x<height>` in pixels.
        print(f"mock_driver: --driver-windowed takes WxH, got {windowed!r}", file=sys.stderr)
        return 2
    userdir = option(argv, "--userdir") or ""
    world = option(argv, "--world") or ""
    scenes_dir = option(argv, "--driver-scenes") or os.path.join(
        option(argv, "--basepath") or "", "tools", "visual_verify", "scenes"
    )
    try:
        deny = load_deny_list(option(argv, "--driver-deny-list"))
    except (OSError, ValueError, KeyError) as e:
        print(f"mock_driver: cannot load the deny list: {e}", file=sys.stderr)
        return 2
    debug_log = DebugLog(userdir, world)
    atexit.register(debug_log.close)
    if fd == "0":
        # As the game does: answer on a private copy of stdout, and move stdout onto stderr.
        chan_in = os.fdopen(0, "rb", buffering=0)
        chan_out = os.fdopen(os.dup(1), "wb", buffering=0)
        os.dup2(2, 1)
    else:
        chan_in = chan_out = os.fdopen(int(fd), "r+b", buffering=0)
    # Output on the game's own stdout must never reach the protocol channel.
    print("MOCK STDOUT NOISE (must never reach the client)", flush=True)
    # A real game takes seconds to boot; MOCK_BOOT_DELAY_S makes starts overlap in tests.
    time.sleep(float(os.environ.get("MOCK_BOOT_DELAY_S", "0")))
    debug_log.log_script("mock_log_boot.txt")
    game = Game(deny)
    game.diverge = os.path.exists(os.path.join(userdir, "save", world, "mock_diverge"))
    quit_crash = os.path.exists(os.path.join(userdir, "save", world, "mock_quit_crash"))
    ready = False

    def reply(resp: dict) -> None:
        chan_out.write((json.dumps(resp) + "\n").encode())

    for raw in chan_in:
        line = raw.decode("utf-8").strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError as e:
            reply(error(None, f"malformed request: {e}"))
            continue
        rid = req.get("id") if isinstance(req, dict) else None
        if not isinstance(req, dict) or not isinstance(req.get("cmd"), str):
            reply(error(rid, "request needs a string `cmd`"))
            continue
        cmd = req["cmd"]
        if cmd == "ping":
            reply({"id": rid, "status": "ok", "ready": True})
            if not ready:
                ready = True
                debug_log.log_script_later("mock_log_idle.txt")
        elif cmd == "state":
            reply(observation(rid, game))
        elif cmd == "wait":
            reply(wait(rid, game, req))
        elif cmd == "move":
            reply(move(rid, game, req))
        elif cmd == "seed":
            reply(seed(rid, req))
        elif cmd == "set_time":
            reply(set_time(rid, game, req))
        elif cmd == "view":
            reply(view(rid, game, req))
        elif cmd == "run_scene":
            reply(run_scene(rid, game, req, scenes_dir))
        elif cmd == "attach_view":
            reply(attach_view(rid, game, req))
        elif cmd == "action":
            reply(action(rid, game, req))
        elif cmd == "key":
            reply(key(rid, game, req))
        elif cmd == "capture":
            reply(capture(rid, game, req, windowed))
        elif cmd == "render":
            reply(render(rid, game, req))
        elif cmd == "minimise":
            game.minimised = True
            reply({"id": rid, "status": "ok"})
        elif cmd == "restore":
            game.minimised = False
            reply({"id": rid, "status": "ok"})
        elif cmd == "quit":
            reply({"id": rid, "status": "ok"})
            time.sleep(DebugLog.QUIT_DELAY_S)
            debug_log.log_script("mock_log_quit.txt")
            if quit_crash:
                crash()
            return 0
        elif cmd == "info":
            reply({"id": rid, "status": "ok", "userdir": userdir, "world": world, "pid": os.getpid()})
        elif cmd == "dirty":
            with open(os.path.join(userdir, "save", world, "scribble"), "w", newline="\n") as f:
                f.write("written by the mock\n")
            reply({"id": rid, "status": "ok"})
        elif cmd == "log":
            debug_log.write(str(req.get("text", "")))
            reply({"id": rid, "status": "ok"})
        elif cmd == "hurt":
            game.hp -= int(req.get("amount", 1))
            reply(observation(rid, game))
        elif cmd == "rewind":
            game.turn -= int(req.get("turns", 1))
            reply(observation(rid, game, time_passed=True))
        elif cmd == "glitch":
            game.turn += 1
            reply(observation(rid, game))
        elif cmd == "spawn_child":
            child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(311)"])
            reply({"id": rid, "status": "ok", "child_pid": child.pid})
        elif cmd in COMBAT_REACH:
            reply(combat(rid, game, req, cmd))
        elif cmd == "sleep":
            time.sleep(float(req.get("seconds", 1)))
            reply({"id": rid, "status": "ok"})
        elif cmd == "hang":
            while True:
                time.sleep(3600)
        else:
            reply(error(rid, f"unknown command {cmd!r}"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
