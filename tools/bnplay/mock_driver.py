#!/usr/bin/python3
"""Mock game driver: speaks the real `--driver-fd` wire protocol without a game.

The contract suites run against this and against the real binary, so the supervisor can be tested
end to end without a 7 to 10 second, 1 GB boot. It is started exactly like the game (the fd shim
execs it with `--driver-fd N` appended) and accepts the game's other flags, including
`--driver-deny-list <file>`.

Protocol commands (same as the real driver): `ping`, `state`, `wait`, `move`, `seed`, `action`,
`key`, `quit`. The mock world is the contract's fixture: the avatar is walled in on every compass
side and cannot go up, so a move is blocked or refused and costs no time; `wait` and a raw `pause`
spend turns; `inventory`, `look` and `map` open menus that a `key` answers.
`view` answers for the same walled-in fixture (the avatar's neighbours are walls, the rest is out
of sight, and a rock lies underfoot), and `attach_view` adds that view to every observation.

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
Env `MOCK_BOOT_DELAY_S` delays the first answer, like a slow boot.

Like the game it writes `<userdir>/config/debug.log`, buffered: nothing reaches the file until the
process exits. Each line starts with the game's `HH:MM:SS.mmm` wall-clock stamp. The world save
(the fixture) scripts what is logged, one `LEVEL : text` line per file line, so a fixture decides
its own noise: `mock_log_boot.txt` is logged during boot, before the first ping is answered;
`mock_log_idle.txt` shortly after it is answered; `mock_log_quit.txt` while shutting down after
`quit`. A world with `mock_log_none` writes no debug.log at all.

A world with `mock_diverge` makes the first `wait` report a random `pain`, so two same-seed Episodes
of it disagree there, as the real game's same-seed Episodes sometimes do.
"""
from __future__ import annotations

import atexit
import json
import os
import random
import subprocess
import sys
import threading
import time
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


def main() -> int:
    argv = sys.argv[1:]
    fd = option(argv, "--driver-fd")
    if fd is None:
        print("mock_driver: --driver-fd required", file=sys.stderr)
        return 2
    userdir = option(argv, "--userdir") or ""
    world = option(argv, "--world") or ""
    try:
        deny = load_deny_list(option(argv, "--driver-deny-list"))
    except (OSError, ValueError, KeyError) as e:
        print(f"mock_driver: cannot load the deny list: {e}", file=sys.stderr)
        return 2
    debug_log = DebugLog(userdir, world)
    atexit.register(debug_log.close)
    # Output on the game's own stdout must never reach the protocol channel.
    print("MOCK STDOUT NOISE (must never reach the client)", flush=True)
    # A real game takes seconds to boot; MOCK_BOOT_DELAY_S makes starts overlap in tests.
    time.sleep(float(os.environ.get("MOCK_BOOT_DELAY_S", "0")))
    debug_log.log_script("mock_log_boot.txt")
    chan = os.fdopen(int(fd), "r+b", buffering=0)
    game = Game(deny)
    game.diverge = os.path.exists(os.path.join(userdir, "save", world, "mock_diverge"))
    ready = False

    def reply(resp: dict) -> None:
        chan.write((json.dumps(resp) + "\n").encode())

    for raw in chan:
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
        elif cmd == "view":
            reply(view(rid, game, req))
        elif cmd == "attach_view":
            reply(attach_view(rid, game, req))
        elif cmd == "action":
            reply(action(rid, game, req))
        elif cmd == "key":
            reply(key(rid, game, req))
        elif cmd == "quit":
            reply({"id": rid, "status": "ok"})
            time.sleep(DebugLog.QUIT_DELAY_S)
            debug_log.log_script("mock_log_quit.txt")
            return 0
        elif cmd == "info":
            reply({"id": rid, "status": "ok", "userdir": userdir, "world": world, "pid": os.getpid()})
        elif cmd == "dirty":
            with open(os.path.join(userdir, "save", world, "scribble"), "w") as f:
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
            child = subprocess.Popen(["/bin/sleep", "311"])
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
