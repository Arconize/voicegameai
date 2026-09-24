# Horror Voice NPC

A local, offline pipeline: **player voice → speech-to-text → in-character LLM response → txt file your game polls.**

No cloud calls required. Runs entirely on the player's machine.

## Pipeline

```
mic --> AudioCapture (miniaudio, 16kHz mono)
     --> SttEngine (whisper.cpp)                -- transcribes speech to text
     --> PersonaManager                          -- picks character rules for current game section
     --> LlmEngine (llama.cpp)                    -- generates in-character reply
     --> OutputWriter                             -- writes game_state/response.txt + response.ready flag
```

Your game engine only needs to do two simple things:
1. Write the current section id to `game_state/current_section.txt` whenever the player changes area (e.g. `"section_02_basement"`).
2. Touch/create `game_state/listen.flag` whenever you want the app to start listening (e.g. on push-to-talk key down).
3. Poll for `game_state/response.ready`, then read `game_state/response.txt`, then delete `response.ready` (and optionally `response.txt`) once consumed.

That's the entire integration surface - it works the same whether you're in Unity, Unreal, Godot, or a custom engine.

## 1. Build

Requires CMake 3.16+, a C++17 compiler, and git (for fetching dependencies).

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release -j
```

This automatically fetches and builds `whisper.cpp`, `llama.cpp`, `miniaudio`, and `nlohmann/json` via CMake's FetchContent - no manual submodule setup needed. First build will take a while since it's compiling two inference engines.

### If you later get a real GPU (NVIDIA)
Rebuild with CUDA enabled:
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DGGML_CUDA=ON
cmake --build . --config Release -j
```
Then in `src/main.cpp`, bump `n_gpu_layers` up from `0` (try `20`, `999` for "everything", back off if it doesn't fit in VRAM).

## 2. Download models

You need two model files, neither included in this repo (keep them out of git - they're large).

**Speech-to-text (whisper.cpp):**
Download a `.bin` ggml model into `models/`, e.g.:
```bash
curl -L -o models/ggml-small.en.bin \
  https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en.bin
```
- `ggml-base.en.bin` — fastest, lowest accuracy, best for weak CPUs
- `ggml-small.en.bin` — good balance (recommended starting point)
- `ggml-medium.en.bin` — much better accuracy, needs more CPU/RAM

**Character LLM (llama.cpp, GGUF format):**
Pick a small instruct model quantized to Q4_K_M so it runs fine on CPU. Good starting points on Hugging Face:
- `Llama-3.2-3B-Instruct` (Q4_K_M) — light, fast, solid for CPU
- `Phi-3.5-mini-instruct` (Q4_K_M) — also light, good instruction following

Download the `.gguf` file and place it at `models/llama-3.2-3b-instruct-q4_k_m.gguf` (or update the path when running).

## 3. Run

```bash
./build/horror_voice_npc personas game_state models/ggml-small.en.bin models/llama-3.2-3b-instruct-q4_k_m.gguf
```

The app will:
- Load every persona in `personas/*.json`
- Open the mic
- Sit idle, polling `game_state/listen.flag`
- When your game touches that flag, record until the player goes quiet, transcribe, generate an in-character reply using whichever persona matches `game_state/current_section.txt`, and write it to `game_state/response.txt` + `game_state/response.ready`

You can test it without a game at all - just manually create the flag file:
```bash
echo "section_02_basement" > game_state/current_section.txt
touch game_state/listen.flag
```
then talk, and watch `game_state/response.txt` after a few seconds.

## 4. Authoring characters (personas/)

Each `.json` file in `personas/` = one persona, keyed by filename (without `.json`). Fields:

| Field | Purpose |
|---|---|
| `character_name` | Shown in logs, used in the system prompt |
| `backstory` | Who they are, their history |
| `current_scene` | What's happening right now in this section |
| `tone` | How they should sound |
| `speech_rules` | Hard constraints - keep responses short, forbidden topics/words, etc. |
| `max_response_tokens` | Caps reply length (shorter = faster + punchier for horror pacing) |

The three included examples (`section_01_intro`, `section_02_basement`, `section_03_final`) show the *same* character escalating across the game - reuse that pattern, or give totally different characters per section.

## 5. Performance notes for lower-end hardware

- Whisper `small.en` + a 3B Q4 LLM is a reasonable floor for CPU-only and should respond in a few seconds on a modern quad-core.
- If responses feel slow: drop to `ggml-base.en.bin`, drop `max_response_tokens` in your personas, or use a smaller/more aggressively quantized LLM (Q4_K_S or even 1B-class models).
- `n_ctx` in `LlmEngine::loadModel` (default 4096) can be lowered if memory is tight - the system prompt + one exchange doesn't need much context.

## 6. Known rough edges to be aware of

- `llm_engine.cpp` calls llama.cpp's public API surface as of the time this was written. llama.cpp's API does shift between commits since we pull `GIT_TAG master`. If the build fails after fetching a newer commit, check `examples/simple/simple.cpp` inside the fetched `llama.cpp` source (under `build/_deps/llama_cpp-src/`) - the generation loop in `llm_engine.cpp` mirrors that example and is usually the only part that needs a small signature tweak. Pin `GIT_TAG` to a specific commit hash in `CMakeLists.txt` once you have a build you're happy with, so it doesn't shift under you.
- The silence-detection (VAD) in `audio_capture.cpp` is a simple RMS-threshold approach, not a trained voice-activity model. It works fine in a quiet room; if your testing environment is noisy, raise `silence_rms_threshold` in the call to `recordUntilSilence()`.
- Language is hardcoded to English (`"en"`) in `stt_engine.cpp` - change to `"auto"` for multilingual detection, or another language code as needed.
