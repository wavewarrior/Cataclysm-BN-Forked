#!/usr/bin/python3
"""Mock game driver: speaks the real `--driver-fd` wire protocol without a game.

The contract suites run against this and against the real binary, so the supervisor can be tested
end to end without a 7 to 10 second, 1 GB boot. It is started exactly like the game (the fd shim
execs it with `--driver-fd N` appended) and accepts the game's other flags.

Protocol commands (same as the real driver): `ping`, `state`, `wait`, `move`, `seed`, `quit`.
The mock world is the contract's fixture: the avatar is walled in on every compass side and cannot
go up, so a move is blocked or refused and costs no time; only `wait` spends turns.

Test hooks (mock only, never part of the protocol): `info` reports the user directory and world the
mock was started with, `dirty` writes a file into that world, `spawn_child` starts a grandchild in
the process group and reports its pid, `hang` never answers.
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


class Game:
    def __init__(self) -> None:
        self.turn = START_TURN
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


def observation(rid: int, game: Game, **fields) -> dict:
    return {
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
        **fields,
    }


def error(rid: int | None, message: str) -> dict:
    return {"id": rid, "status": "error", "error": message}


def wait(rid: int, game: Game, req: dict) -> dict:
    turns = req.get("turns")
    if not is_int(turns) or turns < 1:
        return error(rid, "`turns` must be a positive integer")
    spent = min(turns, TURN_CAP)
    game.turn += spent
    if turns > TURN_CAP:
        return observation(rid, game, outcome="interrupted", reason="turn_cap", time_passed=True)
    return observation(rid, game, time_passed=True)


def move(rid: int, game: Game, req: dict) -> dict:
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


def main() -> int:
    argv = sys.argv[1:]
    fd = option(argv, "--driver-fd")
    if fd is None:
        print("mock_driver: --driver-fd required", file=sys.stderr)
        return 2
    userdir = option(argv, "--userdir") or ""
    world = option(argv, "--world") or ""
    # Output on the game's own stdout must never reach the protocol channel.
    print("MOCK STDOUT NOISE (must never reach the client)", flush=True)
    chan = os.fdopen(int(fd), "r+b", buffering=0)
    game = Game()

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
        elif cmd == "hang":
            while True:
                time.sleep(3600)
        else:
            reply(error(rid, f"unknown command {cmd!r}"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
