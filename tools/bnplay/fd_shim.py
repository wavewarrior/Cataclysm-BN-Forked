#!/usr/bin/env python3
"""Spawns a game with a bidirectional protocol descriptor and relays it to this process's stdio.

Deno cannot hand a child an extra inherited descriptor, so the driver client launches this shim
instead. The shim makes a socketpair, installs the child end on the requested descriptor number,
execs the command with `--driver-fd <N>` appended, and relays: our stdin -> socket, socket -> our
stdout. The game's own stdin is /dev/null and its stdout/stderr go to our stderr, so nothing but
protocol bytes reaches our stdout.

The shim is a process-group leader: the client kills the whole group with kill(-pid) to reap the
game and anything it forked.

Usage: fd_shim.py [--fd N] -- <command> [args...]
"""
import os
import fcntl
import signal
import socket
import subprocess
import sys
import threading
import time

DEFAULT_FD = 3
GRACE_SECONDS = 3.0


def kill_group() -> None:
    try:
        os.killpg(os.getpgrp(), signal.SIGKILL)
    except OSError:
        os._exit(137)


def make_child_fd(want: int) -> tuple[socket.socket, socket.socket]:
    """Returns (parent end, child end); the child end sits on descriptor `want`."""
    a, b = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
    # Park both ends well above `want` so installing the child end cannot clobber either.
    parent = socket.socket(fileno=fcntl.fcntl(a.fileno(), fcntl.F_DUPFD_CLOEXEC, want + 10))
    child_src = fcntl.fcntl(b.fileno(), fcntl.F_DUPFD_CLOEXEC, want + 10)
    a.close()
    b.close()
    os.dup2(child_src, want)
    os.close(child_src)
    os.set_inheritable(want, True)
    return parent, socket.socket(fileno=want)


def pump(src: int, dst: int) -> None:
    try:
        while True:
            data = os.read(src, 65536)
            if not data:
                break
            os.write(dst, data)
    except OSError:
        pass


def main() -> int:
    argv = sys.argv[1:]
    want = DEFAULT_FD
    if argv and argv[0] == "--fd":
        want = int(argv[1])
        argv = argv[2:]
    if argv and argv[0] == "--":
        argv = argv[1:]
    if not argv:
        print("fd_shim: no command given", file=sys.stderr)
        return 2
    try:
        os.setsid()
    except OSError:
        pass  # already a group leader
    for sig in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, lambda *_: kill_group())

    parent, child = make_child_fd(want)
    proc = subprocess.Popen(
        argv + ["--driver-fd", str(want)],
        pass_fds=(want,),
        stdin=subprocess.DEVNULL,
        stdout=sys.stderr,
        stderr=None,
    )
    child.close()

    def stdin_to_socket() -> None:
        pump(0, parent.fileno())
        # Client closed its end: give the game a moment to honour `quit`, then reap the group.
        deadline = time.monotonic() + GRACE_SECONDS
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                return
            time.sleep(0.05)
        kill_group()

    threading.Thread(target=stdin_to_socket, daemon=True).start()
    out = threading.Thread(target=pump, args=(parent.fileno(), 1), daemon=True)
    out.start()
    rc = proc.wait()
    out.join(timeout=0.5)
    return rc


if __name__ == "__main__":
    sys.exit(main())
