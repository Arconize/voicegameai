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
    fallback_.temperature          = 0.7f;
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
                else if (key == "temperature" || key == "temp")
                    p.temperature = std::stof(val);
            } catch (const std::exception&) {}
        }

        if (p.character_name.empty())   p.character_name = "none";
        if (p.mood.empty())             p.mood = "Stay happy unless the player is rude.";
        if (p.max_response_tokens <= 0) p.max_response_tokens = 60;
        if (p.max_chats <= 0)           p.max_chats = 40;
        if (p.temperature < 0.0f)       p.temperature = 0.0f;
        if (p.temperature > 2.0f)       p.temperature = 2.0f;
        p.chats_remaining = p.max_chats;

        personas_[p.id] = p;
        loaded_any = true;

        std::cout << "[PersonaManager] Loaded '" << p.id << "'"
                  << " (tokens=" << p.max_response_tokens
                  << ", chats=" << p.max_chats
                  << ", temp=" << p.temperature << ")\n";
    }

    if (!loaded_any)
        std::cerr << "[PersonaManager] No .txt files found in " << personas_dir << "\n";
    return loaded_any;
}
