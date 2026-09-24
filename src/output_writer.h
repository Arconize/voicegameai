#pragma once
#include <string>

// Simple file-based contract with the game engine:
//   1. This app writes the character's line to response.txt
//   2. This app then creates (touches) response.ready
//   3. The game polls for response.ready, reads response.txt, then DELETES
//      response.ready (and optionally response.txt) once it's consumed it.
// This keeps the whole thing engine-agnostic - Unity, Unreal, Godot, or a
// homemade engine can all just poll a folder.
class OutputWriter {
public:
    explicit OutputWriter(std::string game_state_dir);

    void writeResponse(const std::string& text);

    // Also useful: write the raw transcribed player line for debugging/logs.
    void writeLastTranscript(const std::string& text);

private:
    std::string dir_;
};
