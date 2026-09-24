#include "persona_manager.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

// Helper to trim leading/trailing whitespace
static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Helper for case-insensitive prefix checking
static bool startsWithIgnoreCase(const std::string& str, const std::string& prefix, std::string& out_val) {
    if (str.size() < prefix.size()) return false;
    std::string str_sub = str.substr(0, prefix.size());
    std::string lower_str = str_sub;
    std::string lower_prefix = prefix;
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);
    std::transform(lower_prefix.begin(), lower_prefix.end(), lower_prefix.begin(), ::tolower);
    if (lower_str == lower_prefix) {
        out_val = trim(str.substr(prefix.size()));
        return true;
    }
    return false;
}

bool PersonaManager::loadAll(const std::string& personas_dir) {
    fallback_.id = "fallback";
    fallback_.character_name = "Unknown Voice";
    fallback_.backstory = "A presence with no clear identity yet.";
    fallback_.current_scene = "An undefined place.";
    fallback_.tone = "quiet, ambiguous";
    fallback_.speech_rules = "Keep responses short and cryptic.";
    fallback_.max_response_tokens = 2048;
    fallback_.max_chats = 40;
    fallback_.chats_remaining = 40;

    if (!fs::exists(personas_dir)) {
        std::cerr << "[PersonaManager] Path not found: " << personas_dir << "\n";
        return false;
    }

    auto parseTextFile = [](const fs::path& path, Persona& p) {
        std::ifstream f(path);
        if (!f.is_open()) return false;

        p.id = path.stem().string();
        p.character_name = "Unknown";
        p.max_response_tokens = 2048;
        p.max_chats = 40; // Default turn limit
        p.chats_remaining = 40;

        std::string line;
        while (std::getline(f, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;

            std::string val;
            if (startsWithIgnoreCase(line, "name:", val) || startsWithIgnoreCase(line, "character_name:", val)) {
                p.character_name = val;
            } else if (startsWithIgnoreCase(line, "backstory:", val)) {
                p.backstory = val;
            } else if (startsWithIgnoreCase(line, "situation:", val) ||
                       startsWithIgnoreCase(line, "current_scene:", val) ||
                       startsWithIgnoreCase(line, "current_situation:", val) ||
                       startsWithIgnoreCase(line, "scene:", val)) {
                p.current_scene = val;
            } else if (startsWithIgnoreCase(line, "tone:", val)) {
                p.tone = val;
            } else if (startsWithIgnoreCase(line, "rules:", val) || startsWithIgnoreCase(line, "speech_rules:", val)) {
                p.speech_rules = val;
            } else if (startsWithIgnoreCase(line, "tokens:", val) || startsWithIgnoreCase(line, "chats:", val)) {
                try {
                    p.max_chats = std::stoi(val);
                    p.chats_remaining = p.max_chats;
                } catch (...) {
                    p.max_chats = 40;
                    p.chats_remaining = 40;
                }
            } else {
                if (!p.backstory.empty()) p.backstory += " ";
                p.backstory += line;
            }
        }
        return true;
    };

    if (fs::is_regular_file(personas_dir)) {
        Persona p;
        if (parseTextFile(personas_dir, p)) {
            personas_[p.id] = p;
            fallback_ = p;
            std::cout << "[PersonaManager] Loaded single text-based persona: " << p.id
                      << " (" << p.character_name << ", Tokens: " << p.max_chats << ")\n";
            return true;
        }
        return false;
    }

    for (const auto& entry : fs::directory_iterator(personas_dir)) {
        if (entry.path().extension() != ".txt") continue;

        Persona p;
        if (parseTextFile(entry.path(), p)) {
            personas_[p.id] = p;
            std::cout << "[PersonaManager] Loaded text-based persona: " << p.id
                      << " (" << p.character_name << ", Tokens: " << p.max_chats << ")\n";
        }
    }

    return !personas_.empty();
}

const Persona& PersonaManager::get(const std::string& id) const {
    auto it = personas_.find(id);
    if (it != personas_.end()) return it->second;
    return fallback_;
}

std::string PersonaManager::buildSystemPrompt(const Persona& p) const {
    std::string prompt;
    prompt += "You are " + p.character_name + ", a character in a horror game. ";
    if (!p.backstory.empty()) {
        prompt += "Backstory: " + p.backstory + " ";
    }
    if (!p.current_scene.empty()) {
        prompt += "Current situation: " + p.current_scene + " ";
    }
    if (!p.tone.empty()) {
        prompt += "Tone: speak in a " + p.tone + " manner. ";
    }
    if (!p.speech_rules.empty()) {
        prompt += "Rules: " + p.speech_rules + " ";
    }
    prompt += "Stay fully in character at all times. Never mention that you are an AI, "
              "a language model, or a game character. Respond only with what "
              + p.character_name + " would say out loud, with no stage directions, "
              "no asterisks, no narration - just spoken dialogue.";
    return prompt;
}

void PersonaManager::resetAllTokens() {
    for (auto& pair : personas_) {
        pair.second.chats_remaining = pair.second.max_chats;
    }
    fallback_.chats_remaining = fallback_.max_chats;
    std::cout << "[PersonaManager] Reset all persona token counters back to maximum limits.\n";
}
