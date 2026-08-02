# `vv` — visual verification without spending image tokens

Drives the installed Windows build, captures frames, and reports **what changed as
numbers**. Screenshots stay on disk. The full selftest below — 6 UI states, 8 frames,
4 assertions — costs **932 tokens** of report. Reading those same frames costs ~14,700;
driving the equivalent through the `computer` tool costs ~9,800 and yields no
measurements at all.

```sh
python tools/visual_verify/vv.py selftest          # prove the harness still works
python tools/visual_verify/vv.py run my.vv         # run a scenario
python tools/visual_verify/vv.py ps                # is the game up, where, focused?
```

## When to use what

| situation | tool |
|---|---|
| repeated A/B, regression, threshold ladder, "did my change do anything" | `vv` |
| you don't know where a control *is* yet | `computer` once, then hard-code the rect |
| the numbers are ambiguous and you must actually look | `vv crop --rect --scale`, then `read` it |

`crop` prints its own token cost. A 300×200 crop is ~80 tokens against ~1,850 for a
1920×1080 frame, so escalate to a crop, never to a full frame.

## Scenario language

Line-based, `#` comments, `${VAR}` substituted from `--var K=V`.

| op | meaning |
|---|---|
| `launch [args] [env:K=V]` | start the install; refuses if an instance is already running |
| `attach` | use the running game instead (e.g. one you started via `hub`) |
| `waitlog RX [timeout=ms]` | block until a regex hits `debug.log` — **the readiness signal** |
| `focus` | Alt-tap + `SetForegroundWindow`, verified |
| `settle [timeout= tol= stable=]` | block until the frame stops changing |
| `noise NAME [n= gap= avg= in=]` | idle frame-to-frame noise floor, as a metric |
| `shot NAME [avg=N gap=ms]` | capture; `avg` means N frames (cancels animation) |
| `rect NAME x,y,w,h` | named region; **decimals = fraction of the frame** |
| `diff A B [as=N] [in=RECT] [thresh=]` | compare, record metric, print ASCII delta grid |
| `stats NAME [in=RECT]` | luma / rgb / std / 8-bin histogram |
| `assert m.field OP number\|m.field[*k]` | pass/fail |
| `key "..."`, `mouse …`, `wait ms` | input |
| `log [mark]`, `stop`, `clean-save` | log digest, teardown, save hygiene |

Keys: `{ESC} {ENTER} {UP} {F7} {CTRL+S} {DOWN*3}` are named (scancode + extended flag);
everything else is injected as Unicode text.

## Four things this harness learned the hard way

**1. Averaging, not thresholding, is what defeats animation.** This game's menu moves
~2.5% of pixels per frame on its own — the same order as a real UI change. No threshold
separates them. `avg=12` drops the same-state null from ~7,300 px to ~400 while a real
selection move stays ~1,750.

**2. The null hypothesis is a paired repeat, never a constant.** Capture one state twice
back to back and assert against *that*. A free-running noise floor is bursty (measured:
515 → 2,842 px across four idle samples), so its max is an outlier that hides real
signal. Every threshold in `selftest.vv` is `same_state.changed * 2`.

**3. Readiness comes from the log, not from pixels.** A static loading screen is
"settled" too. `settle` alone reported ready 1 s after launch, ~2.4 s before
`mainmenu.rml` actually opened.

**4. A lone before/after proves nothing.** It equally supports "it worked" and "the input
never arrived". Use the 1 → 0 → 1 triplet: the restore must come back *identical*
(`selftest.vv` regularly measures 0 changed pixels across a `{DOWN}`/`{UP}` pair).

## Traps that cost real debugging time

- **`combined = max(tint.rgb, gpu_total)` in `sprite.frag`.** Outdoors in daylight the CPU
  lightmap dominates and a working GPU lighting change moves 0.3% of pixels. Build the
  scene at night with a carried light before believing a null result.
- **Window size is not stable.** This install came up 1920×1080 and 2560×1440 (physical,
  133% scaling) on consecutive launches. Always write rects as fractions. Capture is
  clamped to the virtual screen, because a client rect larger than the desktop otherwise
  pads with black and shifts every coordinate.
- **A menu hotkey for the already-selected entry is a no-op** (`main_menu.cpp:779` gates on
  `sel1 != i`), and this menu opens on *Load*. `n` for "New Game" changes nothing — it is
  not evidence that input is broken.
- **`{ESC}` on the main menu opens "Really quit?"**, and a later `{ENTER}` accepts it. The
  game exits cleanly and looks exactly like a crash.
- **`--dont-debugmsg` is mandatory unattended.** A mod-heavy world raises thousands of
  modal JSON prompts (2,124 in a 30 s run here) and blocks forever without it.
