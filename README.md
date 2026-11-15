# Horror Voice NPC (fixed version)

A local, offline pipeline: **player voice -> speech-to-text -> in-character LLM
response -> txt file your game polls.** No cloud calls required.

```
mic --> AudioCapture (miniaudio, 16kHz mono)
     --> SttEngine (whisper.cpp)
     --> PersonaManager
     --> LlmEngine (llama.cpp)  -- now with real conversation memory
     --> OutputWriter           -- game_state/response.txt + response.ready
```

Your game engine only needs to:

1. Write the current section id to `game_state/current_section.txt` when the
   player changes area (e.g. `"section_02_basement"`).
2. Create `game_state/listen.flag` to start a conversation, delete it to end one.
3. Poll for `game_state/response.ready`, read `game_state/response.txt`, then
   delete both once consumed.

## What was fixed in this version

1. **Correct chat template** - the prompt is now formatted with the model's OWN
   template via `llama_chat_apply_template()` (read from the GGUF metadata)
   instead of a hardcoded Qwen `<|im_start|>` template. This is the #1 reason
   answers felt unnatural before: Llama/Phi models don't understand ChatML
   tokens. Works with Llama-3.x, Qwen, Phi, Mistral, etc.
2. **Conversation memory** - `LlmEngine` keeps the full dialogue history across
   turns (oldest exchanges are dropped automatically if context fills up), and
   history + KV cache are wiped when a conversation ends. No more "acts dumb
   after a few dialogues".
3. **Sane defaults** - `max_response_tokens` defaults to **120** (was 2048), so
   replies stay short, punchy, and fast. Persona files can override it.
4. **Turn limit no longer silences the NPC** - when `chats_remaining` hits 0 the
   character gives one final in-character line instead of going mute.
5. **Better sampling** - repetition penalty (1.15) + temp 0.7 + fresh random
   seed per reply; the old fixed seed + temp 0.8 caused rambling and loops.
6. **No forced ALL-CAPS**, no duplicate `last_response.txt` writing inside the
   LLM engine (OutputWriter owns output), and the non-Windows build now
   actually records (hands-free silence detection; Windows keeps push-to-talk
   on the V key).
7. Persona loader now also accepts a `max_response_tokens:` field.

## Build

Requires CMake 3.16+, a C++17 compiler, and git.

```
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release -j
```

With an NVIDIA GPU:

```
cmake .. -DCMAKE_BUILD_TYPE=Release -DGGML_CUDA=ON
cmake --build . --config Release -j
```

then set `n_gpu_layers = 20` (or `999`) in `src/main.cpp`.

> llama.cpp's API moves between commits. Once you have a working build, pin
> `GIT_TAG` in `CMakeLists.txt` to the exact commit so it can't shift under you.

## Models

Download into `models/` (keep them out of git):

```
# Speech-to-text (pick one)
curl -L -o models/ggml-small.en.bin   https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en.bin

# LLM (GGUF, Q4_K_M recommended, e.g. Llama-3.2-3B-Instruct) - from Hugging Face
```

## Run

```
./build/horror_voice_npc personas game_state models/ggml-small.en.bin models/llama-3.2-3b-instruct-q4_k_m.gguf
```

## Persona files (personas/*.txt)

One `.txt` file per section. Keys (all optional except it helps to set name):

| Key | Purpose |
|---|---|
| `name:` | Character name, used in the system prompt |
| `backstory:` | Who they are |
| `situation:` (or `scene:`) | What is happening right now |
| `tone:` | How they should sound |
| `rules:` | Hard constraints (brevity, forbidden topics...) |
| `tokens:` (or `chats:`) | Max turns before the final-line behavior kicks in |
| `max_response_tokens:` | Cap on reply length (default 120) |

Lines starting with `#` are comments; unknown keys append to the backstory.
See the three included examples.

## Notes

- Language is hardcoded to `"en"` in `stt_engine.cpp` - change to `"auto"` for
  multilingual input.
- The silence detector is a simple RMS threshold; raise it in noisy rooms
  (see the `recordUntilSilence` call in `main.cpp`).
