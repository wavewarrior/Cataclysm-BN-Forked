#!/usr/bin/env python3
"""vv - token-cheap visual verification harness for Cataclysm-BN (Windows).

The point of this tool is that *screenshots never have to enter an agent's context*.
It drives the installed build, captures frames, and reports what changed as a handful
of numbers plus a tiny ASCII delta grid. Looking at one 1920x1080 frame costs ~1850
image tokens (a `computer` call, ~1230); the report below costs ~150 and says where
the change is. Escalate to `crop` -- and an actual image read -- only when the numbers
are genuinely ambiguous.

  vv.py run SCENARIO [--var k=v]...   execute a scenario file
  vv.py shot [--out P]                capture one frame
  vv.py diff A B                      compare two frames
  vv.py stats IMG                     region statistics of one frame
  vv.py crop IMG --rect x,y,w,h       escalation: small zoomed PNG worth looking at
  vv.py log [--offset N]              compact debug.log digest
  vv.py ps                            is the game running / where is its window
  vv.py selftest                      prove both input paths reach the game

Requires numpy + Pillow. Windows only (ctypes/user32).
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import re
import shlex
import subprocess
import sys
import time
from ctypes import wintypes
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
from PIL import Image, ImageGrab

REPO = Path(__file__).resolve().parents[2]
DEFAULT_INSTALL = REPO / "out" / "install" / "windows-tiles-sounds-x64-msvc"
DEFAULT_RUNS = REPO / "out" / "vv"
PROC_NAME = "cataclysm-bn-tiles"

# --------------------------------------------------------------------------- win32

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
ULONG_PTR = ctypes.c_uint64 if ctypes.sizeof(ctypes.c_void_p) == 8 else ctypes.c_uint32

# Set once, at import, before any window or metric is queried. Doing it lazily made the
# same install report a 1920x1080 client on one run and 2560x1440 (physical, 133% scaling)
# on the next, which silently invalidates every hard-coded coordinate.
try:
    user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))  # PER_MONITOR_AWARE_V2
except (AttributeError, OSError):
    user32.SetProcessDPIAware()


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wintypes.LONG), ("dy", wintypes.LONG), ("mouseData", wintypes.DWORD),
                ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD), ("dwExtraInfo", ULONG_PTR)]


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wintypes.WORD), ("wScan", wintypes.WORD), ("dwFlags", wintypes.DWORD),
                ("time", wintypes.DWORD), ("dwExtraInfo", ULONG_PTR)]


class HARDWAREINPUT(ctypes.Structure):
    _fields_ = [("uMsg", wintypes.DWORD), ("wParamL", wintypes.WORD), ("wParamH", wintypes.WORD)]


class _INPUTUNION(ctypes.Union):
    _fields_ = [("mi", MOUSEINPUT), ("ki", KEYBDINPUT), ("hi", HARDWAREINPUT)]


class INPUT(ctypes.Structure):
    _anonymous_ = ("u",)
    _fields_ = [("type", wintypes.DWORD), ("u", _INPUTUNION)]


INPUT_MOUSE, INPUT_KEYBOARD = 0, 1
KEYEVENTF_EXTENDEDKEY, KEYEVENTF_KEYUP = 0x0001, 0x0002
KEYEVENTF_UNICODE, KEYEVENTF_SCANCODE = 0x0004, 0x0008
MAPVK_VK_TO_VSC = 0
MOUSEEVENTF = {"lmb": (0x0002, 0x0004), "rmb": (0x0008, 0x0010), "mmb": (0x0020, 0x0040)}
SW_RESTORE = 9

VK = {
    "ESC": 0x1B, "ESCAPE": 0x1B, "ENTER": 0x0D, "RETURN": 0x0D, "TAB": 0x09, "SPACE": 0x20,
    "BS": 0x08, "BACKSPACE": 0x08, "DEL": 0x2E, "DELETE": 0x2E, "INS": 0x2D, "INSERT": 0x2D,
    "UP": 0x26, "DOWN": 0x28, "LEFT": 0x25, "RIGHT": 0x27,
    "HOME": 0x24, "END": 0x23, "PGUP": 0x21, "PGDN": 0x22,
    "SHIFT": 0x10, "CTRL": 0x11, "ALT": 0x12,
}
VK.update({f"F{i}": 0x6F + i for i in range(1, 13)})

# Extended-key set. Without KEYEVENTF_EXTENDEDKEY Windows delivers the *numpad* scancode
# for these, and sdl_input.cpp:676 special-cases SDLK_KP_* -- arrows would move nothing.
EXTENDED = {0x26, 0x28, 0x25, 0x27, 0x24, 0x23, 0x21, 0x22, 0x2D, 0x2E, 0x0D}


def _send(*inputs: INPUT) -> None:
    arr = (INPUT * len(inputs))(*inputs)
    n = user32.SendInput(len(inputs), ctypes.byref(arr), ctypes.sizeof(INPUT))
    if n != len(inputs):
        raise OSError(f"SendInput sent {n}/{len(inputs)}: {ctypes.get_last_error()}")


def _vk_input(vk: int, up: bool) -> INPUT:
    """Scancode-based, so SDL's WM_KEYDOWN handler sees a real hardware scancode in
    lParam and resolves ev.key.key correctly."""
    flags = KEYEVENTF_SCANCODE | (KEYEVENTF_KEYUP if up else 0)
    if vk in EXTENDED:
        flags |= KEYEVENTF_EXTENDEDKEY
    return INPUT(type=INPUT_KEYBOARD,
                 ki=KEYBDINPUT(wVk=0, wScan=user32.MapVirtualKeyW(vk, MAPVK_VK_TO_VSC),
                               dwFlags=flags, time=0, dwExtraInfo=0))


def _unicode_input(ch: str, up: bool) -> INPUT:
    """VK_PACKET -> WM_CHAR -> SDL_EVENT_TEXT_INPUT -> UTF8_getch (sdl_input.cpp:700).
    Layout-proof, so no VkKeyScan/`chr:34` hacks. Valid only because sdl_window.cpp:102
    calls SDL_StartTextInput once and desktop never stops it."""
    flags = KEYEVENTF_UNICODE | (KEYEVENTF_KEYUP if up else 0)
    return INPUT(type=INPUT_KEYBOARD,
                 ki=KEYBDINPUT(wVk=0, wScan=ord(ch), dwFlags=flags, time=0, dwExtraInfo=0))


TOKEN_RE = re.compile(r"\{([^}]*)\}")


def send_keys(spec: str, gap_ms: int = 70) -> None:
    """`{NAME}` / `{CTRL+S}` / `{DOWN*3}` are named keys sent as scancodes; every other
    character is injected as Unicode text."""
    def literal(s: str) -> None:
        for ch in s:
            _send(_unicode_input(ch, False), _unicode_input(ch, True))
            time.sleep(gap_ms / 1000)

    pos = 0
    for m in TOKEN_RE.finditer(spec):
        literal(spec[pos:m.start()])
        body, _, rep = m.group(1).upper().partition("*")
        parts = [p.strip() for p in body.split("+") if p.strip()]
        if not parts:
            raise KeyError("empty {} key token")
        mods, main = parts[:-1], parts[-1]
        for _ in range(int(rep) if rep else 1):
            for mod in mods:
                _send(_vk_input(VK[mod], False))
            if main in VK:
                _send(_vk_input(VK[main], False), _vk_input(VK[main], True))
            elif len(main) == 1:
                vk = user32.VkKeyScanW(ord(main.lower())) & 0xFF
                _send(_vk_input(vk, False), _vk_input(vk, True))
            else:
                raise KeyError(f"unknown key {{{body}}}")
            for mod in reversed(mods):
                _send(_vk_input(VK[mod], True))
            time.sleep(gap_ms / 1000)
        pos = m.end()
    literal(spec[pos:])


def mouse_move(x: int, y: int) -> None:
    user32.SetCursorPos(int(x), int(y))


def mouse_button(button: str, action: str = "click") -> None:
    down, up = MOUSEEVENTF[button]
    for f in {"down": [down], "up": [up]}.get(action, [down, up]):
        _send(INPUT(type=INPUT_MOUSE, mi=MOUSEINPUT(0, 0, 0, f, 0, 0)))
        time.sleep(0.02)


def mouse_scroll(clicks: int) -> None:
    _send(INPUT(type=INPUT_MOUSE, mi=MOUSEINPUT(0, 0, 120 * clicks, 0x0800, 0, 0)))


@dataclass
class Win:
    hwnd: int
    pid: int
    title: str
    rect: tuple[int, int, int, int]  # client area in screen coords: l,t,r,b


def _pid_exe(pid: int) -> str | None:
    h = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return None
    try:
        size = wintypes.DWORD(1024)
        buf = ctypes.create_unicode_buffer(size.value)
        return buf.value if kernel32.QueryFullProcessImageNameW(
            h, 0, buf, ctypes.byref(size)) else None
    finally:
        kernel32.CloseHandle(h)


def find_window(proc_name: str = PROC_NAME) -> Win | None:
    found: list[Win] = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, _lparam):
        if not user32.IsWindowVisible(hwnd):
            return True
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if Path(_pid_exe(pid.value) or "").stem.lower() != proc_name.lower():
            return True
        n = user32.GetWindowTextLengthW(hwnd)
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, buf, n + 1)
        r, pt = wintypes.RECT(), wintypes.POINT(0, 0)
        user32.GetClientRect(hwnd, ctypes.byref(r))
        user32.ClientToScreen(hwnd, ctypes.byref(pt))
        if r.right - r.left < 64:
            return True
        found.append(Win(hwnd, pid.value, buf.value,
                         (pt.x, pt.y, pt.x + r.right - r.left, pt.y + r.bottom - r.top)))
        return False

    user32.EnumWindows(cb, 0)
    return found[0] if found else None


def focus(win: Win, tries: int = 8) -> bool:
    """SetForegroundWindow is refused without foreground-activation rights; one piece of
    synthetic input (the Alt tap) grants them. Always verified, never assumed."""
    for t in range(tries):
        _send(_vk_input(VK["ALT"], False), _vk_input(VK["ALT"], True))
        time.sleep(0.2)
        user32.ShowWindow(win.hwnd, SW_RESTORE)
        user32.BringWindowToTop(win.hwnd)
        user32.SetForegroundWindow(win.hwnd)
        time.sleep(0.45)
        if user32.GetForegroundWindow() == win.hwnd:
            return True
        if t >= 2:  # fallback: click an empty corner of the window itself
            mouse_move(win.rect[2] - 40, win.rect[1] + 40)
            time.sleep(0.15)
            mouse_button("lmb")
            time.sleep(0.4)
            if user32.GetForegroundWindow() == win.hwnd:
                return True
    return False


def is_focused(win: Win) -> bool:
    return user32.GetForegroundWindow() == win.hwnd


# --------------------------------------------------------------------------- capture

def virtual_screen() -> tuple[int, int, int, int]:
    x, y = user32.GetSystemMetrics(76), user32.GetSystemMetrics(77)
    return x, y, x + user32.GetSystemMetrics(78), y + user32.GetSystemMetrics(79)


def clamp_bbox(bbox: tuple[int, int, int, int] | None) -> tuple[int, int, int, int] | None:
    """The game can come up larger than the display (seen: a 2560x1440 client on a
    1920x1080 desktop). Grabbing the unclamped rect pads with black and silently shifts
    every coordinate, so intersect with what actually exists."""
    if bbox is None:
        return None
    vl, vt, vr, vb = virtual_screen()
    l, t, r, b = max(bbox[0], vl), max(bbox[1], vt), min(bbox[2], vr), min(bbox[3], vb)
    if r <= l or b <= t:
        raise SystemExit(f"window rect {bbox} is entirely off-screen {virtual_screen()}")
    return l, t, r, b


def grab(bbox: tuple[int, int, int, int] | None = None) -> Image.Image:
    return ImageGrab.grab(bbox=clamp_bbox(bbox), all_screens=True).convert("RGB")


def thumb(img: Image.Image, w: int = 256) -> np.ndarray:
    h = max(1, round(img.height * w / img.width))
    return np.asarray(img.convert("L").resize((w, h), Image.BILINEAR), dtype=np.int16)


def settle(bbox, timeout_ms: int = 15000, tol: float = 0.8, poll_ms: int = 220,
           stable: int = 2) -> tuple[bool, int]:
    """Block until the screen stops changing. Replaces guessed sleeps, the main source of
    'the keystroke landed but the frame was captured too early' garbage."""
    t0 = time.time()
    prev, runs = thumb(grab(bbox)), 0
    while (time.time() - t0) * 1000 < timeout_ms:
        time.sleep(poll_ms / 1000)
        cur = thumb(grab(bbox))
        runs = runs + 1 if float(np.abs(cur - prev).mean()) <= tol else 0
        prev = cur
        if runs >= stable:
            return True, int((time.time() - t0) * 1000)
    return False, int((time.time() - t0) * 1000)


def capture(bbox, avg: int = 1, gap_ms: int = 70) -> Image.Image:
    """Mean of `avg` grabs. Animated backdrops (this game's menu emitters, rain, particles)
    move ~2.5% of pixels per frame on their own, which is the same order as a real UI
    change -- counting thresholded pixels cannot tell them apart. Averaging cancels the
    time-varying part and leaves the static change standing."""
    if avg <= 1:
        return grab(bbox)
    acc = np.zeros((*grab(bbox).size[::-1], 3), dtype=np.float64)
    for i in range(avg):
        if i:
            time.sleep(gap_ms / 1000)
        acc += np.asarray(grab(bbox), dtype=np.float64)
    return Image.fromarray((acc / avg).round().astype(np.uint8))


def ahash(img: Image.Image) -> str:
    a = np.asarray(img.convert("L").resize((8, 8), Image.BILINEAR), dtype=np.int16)
    return f"{int(''.join('1' if b else '0' for b in (a > a.mean()).flatten()), 2):016x}"


# --------------------------------------------------------------------------- measure

RAMP = " .:-=+*#%@"


@dataclass
class Diff:
    thresh: int
    changed: int
    total: int
    mean_abs: float
    mean_changed: float
    max_abs: int
    bias: tuple[float, float, float]
    bbox: tuple[int, int, int, int] | None
    grid: list[str]

    @property
    def pct(self) -> float:
        return 100.0 * self.changed / self.total if self.total else 0.0

    def line(self, label: str) -> str:
        if not self.changed:
            return f"{label}: IDENTICAL (thresh {self.thresh})"
        x, y, w, h = self.bbox
        return (f"{label}: chg {self.pct:.2f}% ({self.changed}px) mean|d| {self.mean_abs:.2f} "
                f"(in-changed {self.mean_changed:.1f}) max {self.max_abs} "
                f"bias r{self.bias[0]:+.1f} g{self.bias[1]:+.1f} b{self.bias[2]:+.1f} "
                f"bbox {x},{y} {w}x{h}")

    def as_dict(self) -> dict:
        return {"changed": self.changed, "pct": round(self.pct, 4),
                "mean_abs": round(self.mean_abs, 3), "mean_changed": round(self.mean_changed, 3),
                "max_abs": self.max_abs, "bias": [round(v, 3) for v in self.bias],
                "bbox": self.bbox, "thresh": self.thresh}


def diff(a: Image.Image, b: Image.Image, thresh: int = 8, grid: tuple[int, int] = (24, 12),
         rect: tuple[int, int, int, int] | None = None) -> Diff:
    if a.size != b.size:
        raise SystemExit(f"size mismatch {a.size} vs {b.size}")
    ia = np.asarray(a, dtype=np.int16)
    ib = np.asarray(b, dtype=np.int16)
    if rect:
        x, y, w, h = rect
        ia, ib = ia[y:y + h, x:x + w], ib[y:y + h, x:x + w]
    d = ib - ia
    mag = np.abs(d).max(axis=2)
    mask = mag > thresh
    n = int(mask.sum())
    bbox = None
    if n:
        ys, xs = np.nonzero(mask)
        bbox = (int(xs.min()), int(ys.min()),
                int(xs.max() - xs.min() + 1), int(ys.max() - ys.min() + 1))
    H, W = mag.shape
    gw, gh = grid
    ye = np.linspace(0, H, gh + 1).astype(int)
    xe = np.linspace(0, W, gw + 1).astype(int)
    rows = []
    for r in range(gh):
        cells = []
        for c in range(gw):
            sub = mask[ye[r]:ye[r + 1], xe[c]:xe[c + 1]]
            p = 100.0 * float(sub.mean()) if sub.size else 0.0
            cells.append(RAMP[0] if p == 0 else RAMP[min(9, 1 + int(p / 12.5))])
        rows.append("".join(cells))
    return Diff(thresh=thresh, changed=n, total=int(mag.size), mean_abs=float(np.abs(d).mean()),
                mean_changed=float(mag[mask].mean()) if n else 0.0, max_abs=int(mag.max()),
                bias=tuple(float(v) for v in d.reshape(-1, 3).mean(axis=0)), bbox=bbox, grid=rows)


def stats(img: Image.Image, rect: tuple[int, int, int, int] | None = None) -> dict:
    a = np.asarray(img, dtype=np.float32)
    if rect:
        x, y, w, h = rect
        a = a[y:y + h, x:x + w]
    lum = a @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    hist = np.histogram(lum, bins=8, range=(0, 256))[0]
    px = max(1, lum.size)
    return {"px": int(lum.size),
            "rgb": [round(float(v), 2) for v in a.reshape(-1, 3).mean(axis=0)],
            "luma": round(float(lum.mean()), 2), "std": round(float(lum.std()), 2),
            "min": int(lum.min()), "max": int(lum.max()),
            "hist8": [round(100.0 * int(v) / px, 1) for v in hist]}


def resolve_rect(spec: str, size: tuple[int, int]) -> tuple[int, int, int, int]:
    """A rect written with decimals is a fraction of the frame. The game has come up at
    two different client sizes on this very box, so any absolute rect in a scenario is a
    latent silent-wrong-region bug."""
    w, h = size
    if "." in spec:
        f = [float(v) for v in spec.replace(" ", "").split(",")]
        if len(f) != 4:
            raise SystemExit(f"bad rect {spec!r}, want x,y,w,h")
        return (int(f[0] * w), int(f[1] * h), int(f[2] * w), int(f[3] * h))
    return parse_rect(spec)


def parse_rect(s: str) -> tuple[int, int, int, int]:
    p = [int(v) for v in s.replace(" ", "").split(",")]
    if len(p) != 4:
        raise SystemExit(f"bad rect {s!r}, want x,y,w,h")
    return tuple(p)


# --------------------------------------------------------------------------- log

LOG_MARKERS = [
    ("errors", re.compile(r"\bERROR\b")),
    ("warnings", re.compile(r"\bWARNING\b")),
    ("debugmsg", re.compile(r"drained \d+ debugmsg")),
    ("rml_open", re.compile(r"rmlui_layer: opened document")),
    ("unknown_token", re.compile(r"unknown rcss token")),
    ("shutdown", re.compile(r"Log shutdown\.")),
]


def log_digest(path: Path, offset: int = 0, tail: int = 6) -> str:
    """debug.log grows ~5 MB / 30 s. Never page it; count markers and quote only faults."""
    if not path.exists():
        return f"log MISSING {path}"
    size = path.stat().st_size
    with path.open("r", encoding="utf-8", errors="replace") as fh:
        fh.seek(min(offset, size))
        lines = fh.read().splitlines()
    counts = {name: 0 for name, _ in LOG_MARKERS}
    hits: list[str] = []
    for ln in lines:
        for name, rx in LOG_MARKERS:
            if rx.search(ln):
                counts[name] += 1
                if name in ("errors", "unknown_token") and len(hits) < tail:
                    hits.append(ln.strip()[:160])
    head = (f"log +{size - offset}B {len(lines)} lines " +
            " ".join(f"{k}={v}" for k, v in counts.items() if v))
    return "\n".join([head] + [f"  ! {h}" for h in hits])


# --------------------------------------------------------------------------- scenario

@dataclass
class Ctx:
    name: str
    outdir: Path
    install: Path
    win: Win | None = None
    proc: subprocess.Popen | None = None
    shots: dict[str, Image.Image] = field(default_factory=dict)
    rects: dict[str, str] = field(default_factory=dict)
    metrics: dict[str, dict] = field(default_factory=dict)
    report: list[str] = field(default_factory=list)
    asserts: list[tuple[str, bool, str]] = field(default_factory=list)
    log_offset: int = 0
    grid: tuple[int, int] = (24, 12)
    thresh: int = 8
    started: float = field(default_factory=time.time)
    strict_focus: bool = True

    def say(self, s: str) -> None:
        self.report.append(s)
        print(s, flush=True)

    @property
    def bbox(self):
        return self.win.rect if self.win else None

    def need_win(self) -> Win:
        self.win = self.win or find_window()
        if self.win is None:
            raise SystemExit("NO_WINDOW: game is not running")
        return self.win

    def check_focus(self) -> None:
        if self.strict_focus and self.win and not is_focused(self.win):
            raise SystemExit("LOST_FOCUS: refusing to send input or trust a frame")


def op_launch(ctx: Ctx, args: list[str]) -> None:
    exe = ctx.install / f"{PROC_NAME}.exe"
    if not exe.exists():
        raise SystemExit(f"missing {exe}")
    if find_window():
        raise SystemExit("ALREADY_RUNNING: another instance owns config/ and save/")
    env, argv = dict(os.environ), []
    for a in args:
        if a.startswith("env:"):
            k, _, v = a[4:].partition("=")
            env[k] = v
        else:
            argv.append(a)
    log = ctx.install / "config" / "debug.log"
    ctx.log_offset = 0
    ctx.proc = subprocess.Popen([str(exe), *argv], cwd=str(ctx.install), env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0 = time.time()
    while time.time() - t0 < 240:
        w = find_window()
        if w:
            ctx.win = w
            ctx.say(f"launch: window {w.rect[2]-w.rect[0]}x{w.rect[3]-w.rect[1]} "
                    f"pid {w.pid} after {time.time()-t0:.1f}s")
            return
        if ctx.proc.poll() is not None:
            raise SystemExit(f"EXITED early rc={ctx.proc.returncode}; " +
                             log_digest(log, 0))
        time.sleep(1.0)
    raise SystemExit("TIMEOUT waiting for game window")


def op_attach(ctx: Ctx, _args: list[str]) -> None:
    w = ctx.need_win()
    ctx.say(f"attach: pid {w.pid} client {w.rect}")


def op_focus(ctx: Ctx, _args: list[str]) -> None:
    if not focus(ctx.need_win()):
        raise SystemExit("FOCUS_FAILED")
    ctx.say("focus: OK")


def op_shot(ctx: Ctx, args: list[str]) -> None:
    kw = _kwargs(args[1:])
    ctx.check_focus()
    img = capture(ctx.bbox, int(kw.get("avg", 1)), int(kw.get("gap", 70)))
    img.save(ctx.outdir / f"{args[0]}.png")
    ctx.shots[args[0]] = img
    ctx.say(f"shot {args[0]}: {img.width}x{img.height} ahash {ahash(img)}")


def op_settle(ctx: Ctx, args: list[str]) -> None:
    kw = _kwargs(args)
    ok, ms = settle(ctx.bbox, timeout_ms=int(kw.get("timeout", 15000)),
                    tol=float(kw.get("tol", 0.8)), stable=int(kw.get("stable", 2)))
    ctx.say(f"settle: {'OK' if ok else 'TIMEOUT'} {ms}ms")


def op_diff(ctx: Ctx, args: list[str]) -> None:
    a, b = args[0], args[1]
    kw = _kwargs(args[2:])
    name = kw.get("as", f"{a}>{b}")
    img_a = _shot(ctx, a)
    rect = resolve_rect(ctx.rects[kw["in"]], img_a.size) if "in" in kw else None
    d = diff(img_a, _shot(ctx, b), thresh=int(kw.get("thresh", ctx.thresh)),
             grid=ctx.grid, rect=rect)
    ctx.metrics[name] = d.as_dict()
    scope = f" [{kw['in']}]" if "in" in kw else ""
    ctx.say(d.line(f"diff {name}{scope}"))
    if d.changed and kw.get("grid", "1") != "0":
        for row in d.grid:
            ctx.say(f"  |{row}|")


def op_stats(ctx: Ctx, args: list[str]) -> None:
    kw = _kwargs(args[1:])
    img = _shot(ctx, args[0])
    rect = resolve_rect(ctx.rects[kw["in"]], img.size) if "in" in kw else None
    s = stats(img, rect)
    key = f"{args[0]}@{kw.get('in', 'all')}"
    ctx.metrics[key] = s
    ctx.say(f"stats {key}: luma {s['luma']} std {s['std']} rgb {s['rgb']} "
            f"range {s['min']}-{s['max']} hist {s['hist8']}")

def op_waitlog(ctx: Ctx, args: list[str]) -> None:
    """Block until a regex appears in debug.log. Far sharper than settling on pixels: a
    static loading screen is 'stable' too, so `settle` alone happily reports ready while
    the game is still 30s from the main menu."""
    rx = re.compile(args[0])
    kw = _kwargs(args[1:])
    path = ctx.install / "config" / "debug.log"
    timeout_ms = int(kw.get("timeout", 120000))
    t0 = time.time()
    scan, tail = ctx.log_offset, ""
    while (time.time() - t0) * 1000 < timeout_ms:
        if path.exists() and path.stat().st_size > scan:
            with path.open("r", encoding="utf-8", errors="replace") as fh:
                fh.seek(scan)
                chunk = fh.read()
                scan = fh.tell()   # exact; len(chunk) drifts on replaced bytes
            tail, _, keep = (tail + chunk).rpartition("\n")
            for ln in tail.splitlines():
                if rx.search(ln):
                    ctx.say(f"waitlog {args[0]!r}: hit after "
                            f"{int((time.time() - t0) * 1000)}ms")
                    return
            tail = keep
        time.sleep(0.2)
    raise SystemExit(f"WAITLOG_TIMEOUT {args[0]!r}")


def op_noise(ctx: Ctx, args: list[str]) -> None:
    """Measure the idle frame-to-frame noise floor: animated menus, blinking cursors and
    ticking HUD clocks all move pixels on their own. A delta is only evidence if it clears
    this, so `noise` yields a metric you can assert *against* rather than a magic constant."""
    name = args[0] if args and "=" not in args[0] else "noise"
    kw = _kwargs(args[1:] if name != "noise" else args)
    n, gap = int(kw.get("n", 5)), int(kw.get("gap", 400))
    ctx.check_focus()
    avg = int(kw.get("avg", 1))
    frames = []
    for i in range(n):
        if i:
            time.sleep(gap / 1000)
        frames.append(capture(ctx.bbox, avg, int(kw.get("avggap", 70))))
    rect = resolve_rect(ctx.rects[kw["in"]], frames[0].size) if "in" in kw else None
    pairs = [diff(frames[i - 1], frames[i], thresh=ctx.thresh, grid=(1, 1), rect=rect)
             for i in range(1, n)]
    worst = max(pairs, key=lambda d: d.changed)
    ctx.metrics[name] = {"changed": worst.changed, "pct": round(worst.pct, 4),
                         "mean_abs": round(max(d.mean_abs for d in pairs), 3),
                         "max_abs": max(d.max_abs for d in pairs), "frames": n}
    ctx.say(f"noise {name}: worst {worst.changed}px ({worst.pct:.3f}%) over {n} idle frames "
            f"[{', '.join(str(d.changed) for d in pairs)}]")



CMP = {">": lambda a, b: a > b, "<": lambda a, b: a < b, ">=": lambda a, b: a >= b,
       "<=": lambda a, b: a <= b, "==": lambda a, b: a == b, "!=": lambda a, b: a != b}


RHS_RE = re.compile(r"^([-\d.]+)$|^([\w>@.\-]+)\.(\w+)(?:\s*\*\s*([\d.]+))?$")


def op_assert(ctx: Ctx, args: list[str]) -> None:
    """`metric.field OP number` or `metric.field OP other.field [* factor]` -- the second
    form is how you say 'the signal beat the noise floor' without inventing a constant."""
    expr = " ".join(args)
    m = re.match(r"^\s*([\w>@.\-]+)\.(\w+)\s*(>=|<=|==|!=|>|<)\s*(.+?)\s*$", expr)
    if not m:
        raise SystemExit(f"bad assert {expr!r}; want metric.field OP number|metric.field[*k]")
    got = _metric(ctx, m.group(1), m.group(2))
    r = RHS_RE.match(m.group(4))
    if not r:
        raise SystemExit(f"bad assert rhs {m.group(4)!r}")
    val = (float(r.group(1)) if r.group(1) is not None
           else _metric(ctx, r.group(2), r.group(3)) * float(r.group(4) or 1))
    ok = CMP[m.group(3)](got, val)
    ctx.asserts.append((expr, ok, f"{got:g} vs {val:g}"))
    ctx.say(f"ASSERT {expr} -> {'PASS' if ok else 'FAIL'} ({got:g} vs {val:g})")


def _metric(ctx: Ctx, metric: str, fld: str) -> float:
    v = ctx.metrics.get(metric, {}).get(fld)
    if v is None:
        raise SystemExit(f"no metric {metric}.{fld}; have {sorted(ctx.metrics)}")
    return float(v)


def op_key(ctx: Ctx, args: list[str]) -> None:
    ctx.check_focus()
    for a in args:
        send_keys(a)
        time.sleep(0.12)


def op_wait(_ctx: Ctx, args: list[str]) -> None:
    time.sleep(int(args[0]) / 1000)


def op_mouse(ctx: Ctx, args: list[str]) -> None:
    ctx.check_focus()
    verb = args[0]
    if verb == "move":
        mouse_move(*(int(v) for v in args[1].split(",")))
    elif verb == "scroll":
        mouse_scroll(int(args[1]))
    elif verb in MOUSEEVENTF:
        mouse_button(verb, args[1] if len(args) > 1 else "click")
    elif verb == "drag":
        x1, y1 = (int(v) for v in args[1].split(","))
        x2, y2 = (int(v) for v in args[2].split(","))
        mouse_move(x1, y1)
        time.sleep(0.1)
        mouse_button("lmb", "down")
        for i in range(1, 13):  # RmlUi sliders need a real drag, not a click on the track
            mouse_move(x1 + (x2 - x1) * i // 12, y1 + (y2 - y1) * i // 12)
            time.sleep(0.02)
        mouse_button("lmb", "up")
    else:
        raise SystemExit(f"unknown mouse verb {verb}")


def op_rect(ctx: Ctx, args: list[str]) -> None:
    resolve_rect(args[1], (1000, 1000))  # validate now, resolve per-frame
    ctx.rects[args[0]] = args[1]


def op_set(ctx: Ctx, args: list[str]) -> None:
    kw = _kwargs(args)
    if "grid" in kw:
        ctx.grid = tuple(int(v) for v in kw["grid"].split("x"))
    if "thresh" in kw:
        ctx.thresh = int(kw["thresh"])
    if "strict_focus" in kw:
        ctx.strict_focus = kw["strict_focus"] != "0"


def op_log(ctx: Ctx, args: list[str]) -> None:
    path = ctx.install / "config" / "debug.log"
    if args and args[0] == "mark":
        ctx.log_offset = path.stat().st_size if path.exists() else 0
        ctx.say(f"log mark @{ctx.log_offset}")
    else:
        ctx.say(log_digest(path, ctx.log_offset))


def op_stop(ctx: Ctx, _args: list[str]) -> None:
    w = find_window()
    if ctx.proc and ctx.proc.poll() is None:
        ctx.proc.terminate()
        try:
            ctx.proc.wait(20)
        except subprocess.TimeoutExpired:
            ctx.proc.kill()
    elif w:
        subprocess.run(["taskkill", "/PID", str(w.pid), "/T", "/F"],
                       capture_output=True, check=False)
    time.sleep(1.5)
    ctx.say(f"stop: running={find_window() is not None}")


SAVE_JUNK = ("master.gsav", "artifacts.gsav", "lua_state.json", "uistate.json")


def op_clean_save(ctx: Ctx, _args: list[str]) -> None:
    """Play Now writes a character on exit. Delete only what this run created (mtime after
    run start) so a pre-existing world is never damaged."""
    root = ctx.install / "save"
    removed = []
    if root.exists():
        for f in root.glob("**/*"):
            if not f.is_file() or f.stat().st_mtime < ctx.started:
                continue
            if f.name.startswith("#") or f.name in SAVE_JUNK:
                f.unlink()
                removed.append(str(f.relative_to(root)))
    ctx.say(f"clean-save: removed {len(removed)}" + (f" {removed}" if removed else ""))


def _shot(ctx: Ctx, name: str) -> Image.Image:
    if name in ctx.shots:
        return ctx.shots[name]
    if Path(name).exists():
        return Image.open(name).convert("RGB")
    raise SystemExit(f"no shot {name!r}; have {sorted(ctx.shots)}")


def _kwargs(args: list[str]) -> dict[str, str]:
    out = {}
    for a in args:
        k, _, v = a.partition("=")
        out[k] = v or "1"
    return out

OPS = {"launch": op_launch, "attach": op_attach, "focus": op_focus, "shot": op_shot,
       "settle": op_settle, "waitlog": op_waitlog, "noise": op_noise, "diff": op_diff,
       "stats": op_stats, "assert": op_assert, "key": op_key, "wait": op_wait,
       "mouse": op_mouse, "rect": op_rect, "set": op_set, "log": op_log, "stop": op_stop,
       "clean-save": op_clean_save}


def run_scenario(path: Path, variables: dict[str, str], install: Path,
                 outdir: Path | None = None, as_json: bool = False) -> int:
    text = path.read_text(encoding="utf-8")
    for k, v in variables.items():
        text = text.replace(f"${{{k}}}", v)
    if left := set(re.findall(r"\$\{(\w+)\}", text)):
        raise SystemExit(f"unsubstituted vars: {sorted(left)}")
    outdir = outdir or (DEFAULT_RUNS / path.stem)
    outdir.mkdir(parents=True, exist_ok=True)
    ctx = Ctx(name=path.stem, outdir=outdir, install=install)
    ctx.say(f"== vv {path.stem} {time.strftime('%H:%M:%S')} ==")
    failure = None
    try:
        for lineno, raw in enumerate(text.splitlines(), 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            tok = shlex.split(line)
            if tok[0] not in OPS:
                raise SystemExit(f"line {lineno}: unknown op {tok[0]!r}")
            OPS[tok[0]](ctx, tok[1:])
    except SystemExit as e:
        failure = str(e)
        ctx.say(f"ABORT {failure}")
    finally:
        if ctx.proc and ctx.proc.poll() is None:
            op_stop(ctx, [])
    npass = sum(1 for _, ok, _ in ctx.asserts if ok)
    ok = failure is None and bool(ctx.asserts) and npass == len(ctx.asserts)
    ctx.say(f"RESULT {'PASS' if ok else 'FAIL'} {npass}/{len(ctx.asserts)} asserts "
            f"shots={len(ctx.shots)} dir={outdir}")
    (outdir / "report.txt").write_text("\n".join(ctx.report), encoding="utf-8")
    (outdir / "metrics.json").write_text(json.dumps(
        {"name": path.stem, "vars": variables, "pass": bool(ok), "metrics": ctx.metrics,
         "asserts": [{"expr": e, "pass": p, "got": g} for e, p, g in ctx.asserts]}, indent=1),
        encoding="utf-8")
    if as_json:
        print(json.dumps(ctx.metrics))
    return 0 if ok else 1


# --------------------------------------------------------------------------- cli

def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="vv", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--install", type=Path, default=DEFAULT_INSTALL)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("run")
    p.add_argument("scenario", type=Path)
    p.add_argument("--var", action="append", default=[], metavar="K=V")
    p.add_argument("--out", type=Path)
    p.add_argument("--json", action="store_true")

    p = sub.add_parser("shot")
    p.add_argument("--out", type=Path)
    p.add_argument("--screen", action="store_true", help="whole desktop, not the game window")
    p.add_argument("--settle", action="store_true")

    p = sub.add_parser("diff")
    p.add_argument("a", type=Path)
    p.add_argument("b", type=Path)
    p.add_argument("--thresh", type=int, default=8)
    p.add_argument("--grid", default="24x12")
    p.add_argument("--rect")

    p = sub.add_parser("stats")
    p.add_argument("img", type=Path)
    p.add_argument("--rect")

    p = sub.add_parser("crop")
    p.add_argument("img", type=Path)
    p.add_argument("--rect", required=True)
    p.add_argument("--scale", type=float, default=1)
    p.add_argument("--out", type=Path)

    p = sub.add_parser("log")
    p.add_argument("--offset", type=int, default=0)

    sub.add_parser("ps")
    sub.add_parser("selftest")

    a = ap.parse_args(argv)

    if a.cmd == "run":
        return run_scenario(a.scenario, dict(v.split("=", 1) for v in a.var),
                            a.install, a.out, a.json)
    if a.cmd == "ps":
        w = find_window()
        print(f"running pid={w.pid} client={w.rect} focused={is_focused(w)} title={w.title!r}"
              if w else "not running")
        return 0 if w else 1
    if a.cmd == "shot":
        w = None if a.screen else find_window()
        box = w.rect if w else None
        if a.settle:
            print("settle: %s %dms" % settle(box))
        img = grab(box)
        out = a.out or (DEFAULT_RUNS / f"shot-{time.strftime('%H%M%S')}.png")
        out.parent.mkdir(parents=True, exist_ok=True)
        img.save(out)
        print(f"{out} {img.width}x{img.height} ahash {ahash(img)}")
        return 0
    if a.cmd == "diff":
        d = diff(Image.open(a.a).convert("RGB"), Image.open(a.b).convert("RGB"), thresh=a.thresh,
                 grid=tuple(int(v) for v in a.grid.split("x")),
                 rect=parse_rect(a.rect) if a.rect else None)
        print(d.line(f"{a.a.name}>{a.b.name}"))
        for row in (d.grid if d.changed else []):
            print(f"|{row}|")
        return 0
    if a.cmd == "stats":
        print(json.dumps(stats(Image.open(a.img).convert("RGB"),
                               parse_rect(a.rect) if a.rect else None)))
        return 0
    if a.cmd == "crop":
        x, y, w, h = parse_rect(a.rect)
        img = Image.open(a.img).convert("RGB").crop((x, y, x + w, y + h))
        if a.scale != 1:
            # Downscale (scale < 1) needs an area filter or thin bright features
            # alias away; upscale wants NEAREST to keep pixel edges readable.
            resample = Image.NEAREST if a.scale > 1 else Image.LANCZOS
            img = img.resize((max(1, round(w * a.scale)), max(1, round(h * a.scale))), resample)
        out = a.out or a.img.with_name(f"{a.img.stem}-crop.png")
        img.save(out)
        print(f"{out} {img.width}x{img.height} ~{img.width*img.height//750} image tokens")
        return 0
    if a.cmd == "log":
        print(log_digest(a.install / "config" / "debug.log", a.offset))
        return 0
    if a.cmd == "selftest":
        return run_scenario(Path(__file__).parent / "scenarios" / "selftest.vv", {}, a.install)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
