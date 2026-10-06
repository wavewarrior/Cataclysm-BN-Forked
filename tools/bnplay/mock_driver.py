#!/usr/bin/python3
"""Mock game driver: speaks the real `--driver-fd` wire protocol without a game.

The contract suite runs against this and against the real binary, so the supervisor can be tested
end to end without a 7 to 10 second, 1 GB boot. It is started exactly like the game (the fd shim
execs it with `--driver-fd N` appended) and accepts the game's other flags.

Protocol commands (same as the real driver): `ping`, `state`, `quit`.

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

TURN = 1000
VITALS = {"hp": 100, "pain": 0, "stamina": 100, "hunger": 0, "thirst": 0}


def option(argv: list[str], name: str) -> str | None:
    return argv[argv.index(name) + 1] if name in argv else None


def observation(rid: int) -> dict:
    return {
        "id": rid,
        "status": "ok",
        "outcome": "completed",
        "boundary": "turn_complete",
        "turn": TURN,
        "time_passed": False,
        "new_messages": [],
        "prompt": None,
        **VITALS,
    }


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

    def reply(resp: dict) -> None:
        chan.write((json.dumps(resp) + "\n").encode())

    for raw in chan:
        line = raw.decode("utf-8").strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError as e:
            reply({"id": None, "status": "error", "error": f"malformed request: {e}"})
            continue
        rid = req.get("id") if isinstance(req, dict) else None
        if not isinstance(req, dict) or not isinstance(req.get("cmd"), str):
            reply({"id": rid, "status": "error", "error": "request needs a string `cmd`"})
            continue
        cmd = req["cmd"]
        if cmd == "ping":
            reply({"id": rid, "status": "ok", "ready": True})
        elif cmd == "state":
            reply(observation(rid))
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
            reply({"id": rid, "status": "error", "error": f"unknown command {cmd!r}"})
    return 0


if __name__ == "__main__":
    sys.exit(main())
