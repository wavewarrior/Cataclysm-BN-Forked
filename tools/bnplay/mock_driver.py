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

Test hooks (mock only, never part of the protocol): `info` reports the user directory and world the
mock was started with, `dirty` writes a file into that world, `spawn_child` starts a grandchild in
the process group and reports its pid, `hang` never answers, `sleep` answers after `seconds`.
Env `MOCK_BOOT_DELAY_S` delays the first answer, like a slow boot.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import time

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
# The deny list a driver started without `--driver-deny-list` uses.
DEFAULT_DENY = BLOCKING_READS
NAMED_KEYS = ("ESC", "ENTER", "SPACE", "TAB", "UP", "DOWN", "LEFT", "RIGHT")


class Game:
    def __init__(self, deny: dict[str, str]) -> None:
        self.turn = START_TURN
        self.deny = deny
        self.menu: str | None = None
        # The message log; an identical message in a row merges into one entry with a count.
        self.log: list[list] = []

    def say(self, text: str) -> str:
        """Logs a message and returns the log entry as the player sees it."""
        if self.log and self.log[-1][0] == text:
            self.log[-1][1] += 1
        else:
            self.log.append([text, 1])
        text, count = self.log[-1]
        return text if count == 1 else f"{text} x {count}"


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
    }
    if game.menu:
        obs.update(outcome="awaiting_input", boundary="needs_input", prompt=game.menu)
    obs.update(fields)
    return obs


def error(rid: int | None, message: str) -> dict:
    return {"id": rid, "status": "error", "error": message}


def menu_open(rid: int, game: Game) -> dict:
    return error(rid, f"the {game.menu} menu is open: answer it with `key` first")


def wait(rid: int, game: Game, req: dict) -> dict:
    if game.menu:
        return menu_open(rid, game)
    turns = req.get("turns")
    if not is_int(turns) or turns < 1:
        return error(rid, "`turns` must be a positive integer")
    game.turn += min(turns, TURN_CAP)
    if turns > TURN_CAP:
        return observation(rid, game, outcome="interrupted", reason="turn_cap", time_passed=True)
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
    return error(rid, f"unknown action {name!r}")


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
    # Output on the game's own stdout must never reach the protocol channel.
    print("MOCK STDOUT NOISE (must never reach the client)", flush=True)
    # A real game takes seconds to boot; MOCK_BOOT_DELAY_S makes starts overlap in tests.
    time.sleep(float(os.environ.get("MOCK_BOOT_DELAY_S", "0")))
    chan = os.fdopen(int(fd), "r+b", buffering=0)
    game = Game(deny)

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
        elif cmd == "state":
            reply(observation(rid, game))
        elif cmd == "wait":
            reply(wait(rid, game, req))
        elif cmd == "move":
            reply(move(rid, game, req))
        elif cmd == "seed":
            reply(seed(rid, req))
        elif cmd == "action":
            reply(action(rid, game, req))
        elif cmd == "key":
            reply(key(rid, game, req))
        elif cmd == "quit":
            reply({"id": rid, "status": "ok"})
            return 0
        elif cmd == "info":
            reply({"id": rid, "status": "ok", "userdir": userdir, "world": world, "pid": os.getpid()})
        elif cmd == "dirty":
            with open(os.path.join(userdir, "save", world, "scribble"), "w") as f:
                f.write("written by the mock\n")
            reply({"id": rid, "status": "ok"})
        elif cmd == "spawn_child":
            child = subprocess.Popen(["/bin/sleep", "311"])
            reply({"id": rid, "status": "ok", "child_pid": child.pid})
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
