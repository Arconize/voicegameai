#include "output_writer.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

OutputWriter::OutputWriter(std::string game_state_dir) : dir_(std::move(game_state_dir)) {
    fs::create_directories(dir_);
}

void OutputWriter::writeResponse(const std::string& text) {
    const std::string response_path      = dir_ + "/response.txt";
    const std::string ready_path         = dir_ + "/response.ready";
    const std::string last_response_path = dir_ + "/last_response.txt";

    std::error_code ec;

    // 1. Remove stale ready-flag and delete the old last_response.txt
    fs::remove(ready_path, ec);
    fs::remove(last_response_path, ec);

    // 2. Write to response.txt
    {
        std::ofstream out(response_path, std::ios::trunc);
        out << text;
    }

    // 3. Sleep for 0.05 seconds (50 milliseconds) as specified
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 4. Generate the new last_response.txt file with the fresh response
    {
        std::ofstream out(last_response_path, std::ios::trunc);
        out << text;
    }

    // 5. Touch the ready flag last to signal completion
    std::ofstream(ready_path).put('1');

    std::cout << "[OutputWriter] Wrote response and generated last_response.txt (" << text.size() << " chars)\n";
}

void OutputWriter::writeLastTranscript(const std::string& text) {
    std::ofstream out(dir_ + "/last_transcript.txt", std::ios::trunc);
    out << text;
}
