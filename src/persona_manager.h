#pragma once
#include <string>
#include <unordered_map>

// One "persona" = the character's identity + backstory + behavioral rules
struct Persona {
    std::string id;              // matches filename
    std::string character_name;  // e.g. "The Caretaker"
    std::string backstory;       // free text, fed into system prompt
    std::string current_scene;   // what's happening right now
    std::string tone;            // tone of voice
    std::string speech_rules;    // behavioral rules
    int max_response_tokens = 2048; // default to a high value
    int max_chats = 40;          // maximum allowed chats configured in file
    mutable int chats_remaining = 40; // mutable allows changes within const references
};

class PersonaManager {
public:
    // Loads every *.txt file in the given directory or a single text file.
    bool loadAll(const std::string& personas_dir);

    // Returns the persona matching this id, or a safe fallback if missing.
    const Persona& get(const std::string& id) const;

    // Builds the full system prompt string to feed the LLM for this persona.
    std::string buildSystemPrompt(const Persona& p) const;

    // Resets turn token counters for all personas back to their max limits
    void resetAllTokens();

    bool hasAny() const { return !personas_.empty(); }

private:
    std::unordered_map<std::string, Persona> personas_;
    Persona fallback_;
};
