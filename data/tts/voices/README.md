# Piper TTS Voice Packs

Shipped voices for the Piper TTS backend (`ENABLE_TTS` option). The backend
resolves a voice named `V` to `data/tts/voices/V.onnx` (+ `V.onnx.json`), so
the files here are named to match the `voice_pack` values used in NPC
classes/templates:

| Voice | File | Source (official rhasspy/piper-voices) |
|-------|------|----------------------------------------|
| `female` | `female.onnx` | `en/en_US/amy/medium/en_US-amy-medium.onnx` |
| `male` | `male.onnx` | `en/en_US/ryan/medium/en_US-ryan-medium.onnx` |

Base URL: `https://huggingface.co/rhasspy/piper-voices/resolve/main/`

License: the voice models are released by their respective authors (see the
piper-voices repo metadata); the piper binary itself is MIT.
