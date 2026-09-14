# Piper TTS for NPCs — design

## Status of the scaffolding (already merged, do not redo)

The plan's "currently ABSENT" premise is stale. The following already exist and are
wired:

- `src/tts_synthesizer.{h,cpp}` — abstract `tts_synthesizer_base` + `tts_stub_synthesizer`
  (logs only) + global `g_tts_synthesizer` + `init_tts_synthesizer()`.
- `src/tts_voice_registry.{h,cpp}` — `npc_class_id → voice_name` map, `resolve_voice(npc)`.
- `npc_class::voice_pack_id` (JSON `"voice_pack"`) auto-registers at `finalize_all()`.
- `ENABLE_TTS` option (default **false**, prerequisite `SOUND_ENABLED`).
- `npc::say()` already calls `g_tts_synthesizer->synthesize(line, voice)` when enabled.
- `init_sound()` already calls `init_tts_synthesizer()` when `ENABLE_TTS` is on.

**The only missing piece is a real backend.** This design adds one.

## Decisions (constrained by the APIs, not preference)

| # | Decision | Why |
|---|----------|-----|
| 1 | **Subprocess** `piper`, not linked ONNX Runtime. | Piper is not installed on the build/release targets and bundling `onnxruntime` + a `.onnx` voice across macOS-arm64/Win-x64 is the fragile packaging the plan explicitly warns about. The plan's documented contingency is exactly this: subprocess of a system `piper`, gracefully disabled when absent. |
| 2 | **Worker thread**, not synchronous. | `npc::say()` runs on the game thread inside the turn loop; a subprocess round-trip (100s of ms) there would hitch the game. Synthesis is async; playback is fire-and-forget. |
| 3 | **Dedicated `sfx::channel::tts`** persistent track. | Speech must interrupt the previous utterance (a new line supersedes the old), and must not collide with ambient/env channels. A named channel gives a stable `MIX_Track` to stop-then-play. |
| 4 | **`MIX_PlayAudio` from the worker thread.** | SDL_mixer 3 documents `MIX_PlayAudio` as thread-safe. No main-thread marshalling needed; the worker loads the WAV and plays it directly. |
| 5 | **Lazy capability probe** on first `synthesize`, not at `init_sound`. | `init_sound` runs before options/datadir are fully settled; probing then would race. The piper backend probes on first use and degrades to a logged no-op if the binary or voice model is missing. |

## Binary + voice resolution

- **Binary:** `TTS_PIPER_BIN` option (explicit path) → `datadir()/tts/piper` (bundled) →
  `piper` on `PATH`. First hit wins; cached after the first probe.
- **Voice model:** `datadir()/tts/voices/<voice_name>.onnx` (+ `.onnx.json`). Missing →
  that voice is skipped (logged), not fatal.
- Absent binary ⇒ the whole backend is a no-op (the stub's log line is enough signal).

## Component: `tts_piper_synthesizer`

New `src/tts_piper_synthesizer.{h,cpp}` (picked up by the existing `GLOB_RECURSE` in
`src/CMakeLists.txt` — no CMake edit). Implements `tts_synthesizer_base`.

State (guarded by a mutex):
- `std::thread worker;` + `std::atomic<bool> running;`
- `std::queue<job>` where `job = { std::string text; std::string voice; }`;
- `std::condition_variable cv;`
- `std::string piper_bin;` (resolved once), `bool probed;`

`synthesize(text, voice)`:
1. If `!running`, start the worker thread (idempotent).
2. Push `{text, voice}`; notify. Bounded queue (drop-oldest if > 4 pending) so a burst of
   dialogue can't pile up unbounded subprocesses.

Worker loop:
1. Pop a job (blocks on `cv` until `running == false`).
2. Probe piper once (decision 5). If absent → log once, keep draining (cheap).
3. Build the WAV path: a temp file under `SDL_GetPrefPath`/`tts` (piper writes WAV to a
   file; `--output_raw` is not universally available, so use `--output_file`).
4. `SDL_CreateProcess({"piper", "-m", voice.onnx, "--output_file", wav, NULL}, false)`;
   write `text` to stdin via `SDL_GetProcessInput`; `SDL_ReadProcess` to reap stdout +
   exit code. (Text via stdin avoids shell-quoting the dialogue string.)
5. On exit 0 + non-empty WAV: `SDL_IOStream* io = SDL_LoadFile_IO(wav)` →
   `MIX_LoadAudio_IO(mixer, io, /*predecode=*/true, /*closeio=*/true)` →
   stop the `tts` channel track, `MIX_SetTrackAudio(track, audio)`, `MIX_PlayTrack`.
   `MIX_DestroyAudio(audio)` after the track takes ownership per the mixer's refcount
   contract (verify against the linked SDL_mixer at build time).
6. Delete the temp WAV.

Shutdown: `shutdown_sound()` joins the worker (`running = false; cv.notify; thread.join()`).

## Wiring

- `init_tts_synthesizer()`: if `ENABLE_TTS`, set `g_tts_synthesizer` to a
  `tts_piper_synthesizer` (replaces the stub). The stub stays as the fallback type for
  builds where the piper backend is compiled out.
- `sfx::channel`: add `tts` before `MAX_CHANNEL` (renumber-safe: it's appended, and
  `MAX_CHANNEL` is the sentinel). `channel_tracks` array grows automatically.
- No change to `npc::say()` — it already calls the global.

## Graceful degradation (the acceptance bar)

- `ENABLE_TTS` off (default) ⇒ nothing runs, zero cost.
- Option on, no piper binary ⇒ first `synthesize` logs one warning, thereafter silent no-op.
- Option on, piper present, voice missing ⇒ that line is skipped, logged.
- Any subprocess failure ⇒ logged, never thrown, never blocks the game thread.

## Out of scope (kept for a later pass)

- Bundling the `piper` binary + a voice model into the repo (license is MIT, but the
  binary is platform-specific and large; the subprocess path works with a user-installed
  piper, which is the plan's stated fallback).
- Per-NPC voice variety beyond the existing `npc_class::voice_pack_id` registry.
- Spatial/3D positioning of the voice (the existing `speech` sound event already handles
  the audible "someone spoke" cue; TTS is the intelligible layer on top).
