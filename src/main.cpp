#include "persona_manager.h"
#include "audio_capture.h"
#include "stt_engine.h"
#include "llm_engine.h"
#include "output_writer.h"
#include "bootstrapper.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <vector>
#include <string>
#include <cctype>
#include <cstring>

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

// ── خواندن mood از فایل پرسونا ──
// فقط happy, sad, angry مجازن. هر چیز دیگه → happy
static std::string readMoodFromPersona(const Persona& p) {
    std::string m = p.mood;
    for (auto& c : m) c = (char)std::tolower((unsigned char)c);

    if (m == "happy" || m == "sad" || m == "angry") return m;
    return "happy";
}

// ── حذف تگ‌های mood از متن LLM قبل از ذخیره ──
static std::string stripMoodTags(const std::string& text) {
    std::string s = text;

    const char* tags[] = {
        "[happy]", "[sad]", "[angry]",
        "[Happy]", "[Sad]", "[Angry]",
        "[HAPPY]", "[SAD]", "[ANGRY]"
    };
    for (const char* t : tags) {
        size_t pos;
        while ((pos = s.find(t)) != std::string::npos)
            s.erase(pos, std::strlen(t));
    }

    // حذف فاصله‌های اضافه
    while (s.find("  ") != std::string::npos)
        s.replace(s.find("  "), 2, " ");

    // حذف فاصله و newline از ابتدا و انتها
    size_t a = s.find_first_not_of(" \t\n\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\n\r");
    return s.substr(a, b - a + 1);
}

static void writeMoodResponse(const std::string& dir,
                              const std::string& mood,
                              const std::string& text) {
    std::error_code ec;

    // پاک کردن همه‌ی فایل‌های مرتبط از دور قبل
    const char* moods[] = {"happy", "sad", "angry"};
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto name = entry.path().filename().string();

        // last_response*.txt قبلی رو پاک کن
        if (name.rfind("last_response", 0) == 0 &&
            entry.path().extension() == ".txt") {
            fs::remove(entry.path(), ec);
            continue;
        }
        // فایل‌های mood قبلی رو پاک کن
        for (const char* m : moods) {
            if (name == m) {
                fs::remove(entry.path(), ec);
                break;
            }
        }
    }

    // متن آخرین جواب
    std::ofstream out(dir + "/last_response.txt");
    out << text;
    out.close();

    // فایل خالی mood
    std::ofstream moodFile(dir + "/" + mood);
    // (کاملاً خالی می‌مونه)
}

static void ensureDefaultFiles(const std::string& personas_dir,
                               const std::string& game_state_dir) {
    std::error_code ec;
    fs::create_directories(personas_dir, ec);
    fs::create_directories(game_state_dir, ec);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (!Bootstrapper::extractIfNeeded()) {
        std::cerr << "[Main] Bootstrap failed.\n";
        return 1;
    }

    // ── تعریف یک‌باره‌ی همه‌ی مسیرها ──
    std::string personas_dir   = argc > 1 ? argv[1] : "personas";
    std::string game_state_dir = argc > 2 ? argv[2] : "game_state";
    std::string whisper_model  = argc > 3 ? argv[3] : "models/ggml-small.en.bin";
    std::string llm_model      = argc > 4 ? argv[4] : "models/TypoRPV3-2B.Q4_K_M.gguf";

    ensureDefaultFiles(personas_dir, game_state_dir);

    const std::string section_file = game_state_dir + "/current_section.txt";
    const std::string listen_flag  = game_state_dir + "/listen.flag";

    std::cout << "=== Horror Voice NPC ===\n";

    PersonaManager personas;
    if (!personas.loadAll(personas_dir)) {
        std::cerr << "No personas loaded from " << personas_dir << "\n";
        return 1;
    }

    AudioCapture audio;
    if (!audio.init()) return 1;

    SttEngine stt;
    if (!stt.loadModel(whisper_model)) return 1;

    LlmEngine llm;
    if (!llm.loadModel(llm_model, 4096, 4)) return 1;

    OutputWriter output(game_state_dir);

#ifdef _WIN32
    std::cout << "\nReady.\n"
              << "- Create " << listen_flag << " to start.\n"
              << "- Hold V to record, release V to send.\n"
              << "- Delete " << listen_flag << " to end.\n\n";
#else
    std::cout << "\nReady.\n"
              << "- Create " << listen_flag << " to start.\n"
              << "- Hands-free recording until you stop talking.\n"
              << "- Delete " << listen_flag << " to end.\n\n";
#endif

    auto processTurn = [&](std::vector<float> samples, const std::string& section_id) {
        Persona& persona = personas.get(section_id);

        std::cout << "[Main] Transcribing audio...\n";
        std::string player_text = stt.transcribe(samples);
        std::cout << "[Main] Player said: \"" << player_text << "\"\n";
        if (!player_text.empty()) output.writeLastTranscript(player_text);

        if (player_text.empty()) {
            std::cout << "[Main] Empty transcription, skipping response.\n";
            return;
        }

        std::string system_prompt = personas.buildSystemPrompt(persona);

        if (persona.chats_remaining <= 0) {
            std::cout << "[Main] Turn limit reached - prompting final line.\n";
            system_prompt += "\nThe conversation is ending. One final short line.";
        } else {
            persona.chats_remaining--;
            std::cout << "[Main] Remaining turns: "
                      << persona.chats_remaining << " / " << persona.max_chats << "\n";
        }

        std::cout << "[Main] Generating in-character response...\n";
        std::string response = llm.generate(system_prompt, player_text,
                                            persona.max_response_tokens);
        if (response.empty()) {
            std::cout << "[Main] LLM produced nothing.\n";
            return;
        }

        // mood از فایل پرسونا خونده میشه
        std::string mood = readMoodFromPersona(persona);

        // تگ‌های [happy]/[sad]/[angry] رو از متن LLM پاک کن
        std::string clean = stripMoodTags(response);

        std::cout << "[Main] " << persona.id
                  << " [" << mood << "]: " << clean << "\n";
        writeMoodResponse(game_state_dir, mood, clean);
    };

    std::string last_section_id;
    std::string sectionBuffer;
    bool session_was_active = false;
    bool key_was_down = false;

    while (true) {
        bool session_active = fs::exists(listen_flag);

        if (!session_active) {
            if (session_was_active) {
                std::cout << "[Main] Conversation ended.\n";
                if (key_was_down) { audio.stopRecording(); key_was_down = false; }
                llm.resetHistory();
            }
            session_was_active = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (!session_was_active) {
            std::cout << "[Main] Conversation started.\n";
            personas.resetAllTokens();
            llm.resetHistory();
        }
        session_was_active = true;

        sectionBuffer = readFileTrimmed(section_file);
        if (sectionBuffer.empty()) sectionBuffer = "fallback";
        if (sectionBuffer != last_section_id) {
            std::cout << "[Main] Active section: " << sectionBuffer << "\n";
            last_section_id = sectionBuffer;
        }

#ifdef _WIN32
        bool key_down = isTalkKeyDown();
        if (key_down && !key_was_down) {
            std::cout << "[Main] Recording...\n";
            audio.startRecording();
            key_was_down = true;
        } else if (!key_down && key_was_down) {
            std::vector<float> samples = audio.stopRecording();
            key_was_down = false;
            if (samples.empty()) std::cout << "[Main] Nothing recorded.\n";
            else processTurn(std::move(samples), sectionBuffer);
        }
#else
        (void)key_was_down;
        std::vector<float> samples = audio.recordUntilSilence(15000, 900, 0.01f);
        if (samples.empty()) std::cout << "[Main] Nothing recorded.\n";
        else processTurn(std::move(samples), sectionBuffer);
#endif

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    return 0;
}
