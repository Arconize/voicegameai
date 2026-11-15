#pragma once
#include <string>
#include <unordered_map>

struct Persona {
    std::string id;
    std::string character_name;
    std::string backstory;
    std::string current_scene;
    std::string tone;
    std::string speech_rules;
    std::string mood;              // NEW: how the character picks its mood
    int max_response_tokens = 120;
    int max_chats = 40;
    mutable int chats_remaining = 40;
};

class PersonaManager {
public:
    bool loadAll(const std::string& personas_dir);

    Persona& get(const std::string& id);
    const Persona& get(const std::string& id) const;

    std::string buildSystemPrompt(const Persona& p) const;

    void resetAllTokens();

    bool hasAny() const { return !personas_.empty(); }

private:
    std::unordered_map<std::string, Persona> personas_;
    Persona fallback_;
};
