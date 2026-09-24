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
    return false;
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

    std::cout << "\nReady.\n"
              << "- Create " << listen_flag << " to start the conversation.\n"
              << "- While it exists, hold V to record voice, release V to send.\n"
              << "- Delete " << listen_flag << " to end the conversation.\n\n";

    std::string last_section_id;
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
            }
            session_was_active = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Triggered when conversation transitions from inactive to active
        if (!session_was_active) {
            std::cout << "[Main] Conversation started (listen.flag detected).\n";
            personas.resetAllTokens(); // Reset token allowances
        }
        session_was_active = true;

        std::string section_id = readFileTrimmed(section_file);
        if (section_id.empty()) section_id = "fallback";
        if (section_id != last_section_id) {
            std::cout << "[Main] Active section: " << section_id << "\n";
            last_section_id = section_id;
        }

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
                const Persona& persona = personas.get(section_id);

                std::cout << "[Main] Transcribing audio...\n";
                std::string player_text = stt.transcribe(samples);
                std::cout << "[Main] Player said: \"" << player_text << "\"\n";
                output.writeLastTranscript(player_text);

                if (!player_text.empty()) {
                    // Check remaining turn-token limit [5]
                    if (persona.chats_remaining <= 0) {
                        std::cout << "[Main] Chat token limit reached (0/" << persona.max_chats
                                  << ") for " << persona.character_name << ". Skipping response generation.\n";
                    } else {
                        // Consume one chat token
                        persona.chats_remaining--;
                        std::cout << "[Main] Token consumed. Remaining allowance for "
                                  << persona.character_name << ": " << persona.chats_remaining
                                  << " / " << persona.max_chats << "\n";

                        std::string system_prompt = personas.buildSystemPrompt(persona);

                        std::cout << "[Main] Generating in-character response...\n";
                        std::string response = llm.generate(system_prompt, player_text,
                                                             persona.max_response_tokens);
                        std::cout << "[Main] " << persona.character_name << ": " << response << "\n";

                        output.writeResponse(response);
                    }
                } else {
                    std::cout << "[Main] Empty transcription, skipping response.\n";
                }
            }
        }

        key_was_down = key_down;

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    return 0;
}
