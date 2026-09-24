#include "persona_manager.h"
#include "audio_capture.h"
#include "stt_engine.h"
#include "llm_engine.h"
#include "output_writer.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <filesystem>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

namespace fs = std::filesystem;

static std::string readFileTrimmed(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    return s;
}

static bool isTalkKeyDown() {
#ifdef _WIN32
    return (GetAsyncKeyState('V') & 0x8000) != 0;
#else
    return false; // non-Windows uses hands-free silence detection instead
#endif
}

int main(int argc, char** argv) {
    std::string personas_dir   = argc > 1 ? argv[1] : "personas";
    std::string game_state_dir = argc > 2 ? argv[2] : "game_state";
    std::string whisper_model  = argc > 3 ? argv[3] : "models/ggml-small.en.bin";
    std::string llm_model      = argc > 4 ? argv[4] : "models/llama-3.2-3b-instruct-q4_k_m.gguf";

    int n_gpu_layers = 0;

    fs::create_directories(game_state_dir);
    const std::string section_file = game_state_dir + "/current_section.txt";
    const std::string listen_flag  = game_state_dir + "/listen.flag";

    std::cout << "=== Horror Voice NPC ===\n";

    PersonaManager personas;
    if (!personas.loadAll(personas_dir)) {
        std::cerr << "No personas loaded from " << personas_dir
                  << " - check path exists and contains valid .txt file(s).\n";
        return 1;
    }

    AudioCapture audio;
    if (!audio.init()) return 1;

    SttEngine stt;
    if (!stt.loadModel(whisper_model)) return 1;

    LlmEngine llm;
    if (!llm.loadModel(llm_model, n_gpu_layers)) return 1;

    OutputWriter output(game_state_dir);

#ifdef _WIN32
    std::cout << "\nReady.\n"
              << "- Create " << listen_flag << " to start the conversation.\n"
              << "- While it exists, hold V to record voice, release V to send.\n"
              << "- Delete " << listen_flag << " to end the conversation.\n\n";
#else
    std::cout << "\nReady.\n"
              << "- Create " << listen_flag << " to start the conversation.\n"
              << "- The app then listens hands-free: it records until you stop talking.\n"
              << "- Delete " << listen_flag << " to end the conversation.\n\n";
#endif

    // One full player->NPC exchange: transcribe, generate, write out.
    auto processTurn = [&](std::vector<float> samples) {
        const Persona& persona = personas.get(sectionBuffer);

        std::cout << "[Main] Transcribing audio...\n";
        std::string player_text = stt.transcribe(samples);
        std::cout << "[Main] Player said: \"" << player_text << "\"\n";
        if (!player_text.empty()) output.writeLastTranscript(player_text);

        if (player_text.empty()) {
            std::cout << "[Main] Empty transcription, skipping response.\n";
            return;
        }

        std::string system_prompt = personas.buildSystemPrompt(persona);

        // Turn-limit handling: instead of going silently mute, the character
        // gives one final in-character line so the game never looks broken.
        if (persona.chats_remaining <= 0) {
            std::cout << "[Main] Turn limit reached for " << persona.character_name
                      << " - prompting a final line.\n";
            system_prompt += " The conversation is ending now. Give one final, "
                             "short, in-character line and do not ask a question.";
        } else {
            persona.chats_remaining--;
            std::cout << "[Main] Remaining turns for " << persona.character_name
                      << ": " << persona.chats_remaining << " / " << persona.max_chats << "\n";
        }

        std::cout << "[Main] Generating in-character response...\n";
        std::string response = llm.generate(system_prompt, player_text,
                                            persona.max_response_tokens);
        if (response.empty()) {
            std::cout << "[Main] LLM produced nothing, nothing written.\n";
            return;
        }
        std::cout << "[Main] " << persona.character_name << ": " << response << "\n";
        output.writeResponse(response);
    };

    std::string last_section_id;
    std::string sectionBuffer;   // shared with the processTurn lambda
    bool session_was_active = false;
    bool key_was_down = false;

    while (true) {
        bool session_active = fs::exists(listen_flag);

        if (!session_active) {
            if (session_was_active) {
                std::cout << "[Main] listen.flag removed - conversation ended.\n";
                if (key_was_down) {
                    audio.stopRecording();
                    key_was_down = false;
                }
                // NEW: wipe memory so the next conversation starts fresh
                llm.resetHistory();
            }
            session_was_active = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (!session_was_active) {
            std::cout << "[Main] Conversation started (listen.flag detected).\n";
            personas.resetAllTokens();
            llm.resetHistory(); // defensive: make sure no stale history survives
        }
        session_was_active = true;

        sectionBuffer = readFileTrimmed(section_file);
        if (sectionBuffer.empty()) sectionBuffer = "fallback";
        if (sectionBuffer != last_section_id) {
            std::cout << "[Main] Active section: " << sectionBuffer << "\n";
            last_section_id = sectionBuffer;
        }

#ifdef _WIN32
        // ---- Push-to-talk (Windows): hold V to record ----
        bool key_down = isTalkKeyDown();

        if (key_down && !key_was_down) {
            std::cout << "[Main] Recording... (V held)\n";
            audio.startRecording();
            key_was_down = true;
        } else if (!key_down && key_was_down) {
            std::vector<float> samples = audio.stopRecording();
            key_was_down = false;

            if (samples.empty()) {
                std::cout << "[Main] Nothing recorded, skipping.\n";
            } else {
                processTurn(std::move(samples));
            }
        }
#else
        (void)key_was_down; // only used by the Windows push-to-talk path
        // ---- Hands-free (Linux/macOS): record until the player stops talking ----
        std::vector<float> samples = audio.recordUntilSilence(
            /*max_record_ms*/ 15000,
            /*silence_ms*/ 900,
            /*silence_rms_threshold*/ 0.01f);

        if (samples.empty()) {
            std::cout << "[Main] Nothing recorded, skipping.\n";
        } else {
            processTurn(std::move(samples));
        }
#endif

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    return 0;
}
