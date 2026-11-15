#include "persona_manager.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cctype>

namespace fs = std::filesystem;

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string toLower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool PersonaManager::loadAll(const std::string& personas_dir) {
    personas_.clear();

    fallback_.id                   = "fallback";
    fallback_.character_name       = "none";
    fallback_.backstory            = "";
    fallback_.current_scene        = "You are talking to a stranger.";
    fallback_.tone                 = "Casual, short sentences.";
    fallback_.speech_rules         = "One short line. Stay in character.";
    fallback_.mood                 = "Stay happy unless the player is rude.";
    fallback_.max_response_tokens  = 60;
    fallback_.max_chats            = 40;
    fallback_.chats_remaining      = 40;

    if (!fs::exists(personas_dir)) return false;

    bool loaded_any = false;

    for (const auto& entry : fs::directory_iterator(personas_dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".txt") continue;

        std::ifstream f(entry.path());
        if (!f.is_open()) continue;

        Persona p;
        p.id = entry.path().stem().string();

        std::string line;
        while (std::getline(f, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;

            auto sep = line.find_first_of(":=");
            if (sep == std::string::npos) continue;

            std::string key = toLower(trim(line.substr(0, sep)));
            std::string val = trim(line.substr(sep + 1));
            if (val.empty()) continue;

            try {
                if (key == "name" || key == "character_name")
                    p.character_name = val;
                else if (key == "backstory")
                    p.backstory = val;
                else if (key == "situation" || key == "scene" || key == "current_scene")
                    p.current_scene = val;
                else if (key == "tone")
                    p.tone = val;
                else if (key == "rules" || key == "speech_rules")
                    p.speech_rules = val;
                else if (key == "mood")
                    p.mood = val;
                else if (key == "tokens" || key == "max_response_tokens" || key == "max_tokens")
                    p.max_response_tokens = std::stoi(val);
                else if (key == "max_chats" || key == "chats")
                    p.max_chats = std::stoi(val);
            } catch (const std::exception&) {}
        }

        if (p.character_name.empty()) p.character_name = "none";
        if (p.mood.empty())           p.mood = "Stay happy unless the player is rude.";
        if (p.max_response_tokens <= 0) p.max_response_tokens = 60;
        if (p.max_chats <= 0)           p.max_chats = 40;
        p.chats_remaining = p.max_chats;

        personas_[p.id] = p;
        loaded_any = true;

        std::cout << "[PersonaManager] Loaded '" << p.id << "'"
                  << " (tokens=" << p.max_response_tokens
                  << ", chats=" << p.max_chats << ")\n";
    }

    if (!loaded_any)
        std::cerr << "[PersonaManager] No .txt files found in " << personas_dir << "\n";
    return loaded_any;
}

Persona& PersonaManager::get(const std::string& id) {
    auto it = personas_.find(id);
    return it != personas_.end() ? it->second : fallback_;
}

const Persona& PersonaManager::get(const std::string& id) const {
    auto it = personas_.find(id);
    return it != personas_.end() ? it->second : fallback_;
}

std::string PersonaManager::buildSystemPrompt(const Persona& p) const {
    std::ostringstream ss;

    if (!p.current_scene.empty()) ss << p.current_scene << "\n";
    if (!p.backstory.empty())     ss << p.backstory     << "\n";
    if (!p.tone.empty())          ss << p.tone          << "\n";
    if (!p.speech_rules.empty())  ss << p.speech_rules  << "\n";

    // Mood instructions
    ss << "\nHow to pick your mood: " << p.mood << "\n";
    ss << "\nYou MUST start every reply with one of these mood tags: "
          "[happy] [sad] [angry]\n"
          "Format: [mood] then your spoken line.\n"
          "Example: [angry] You shouldn't have said that.\n"
          "Only use one tag. Never skip the tag. Never explain the tag.\n";

    return ss.str();
}

void PersonaManager::resetAllTokens() {
    for (auto& kv : personas_) kv.second.chats_remaining = kv.second.max_chats;
    fallback_.chats_remaining = fallback_.max_chats;
}
