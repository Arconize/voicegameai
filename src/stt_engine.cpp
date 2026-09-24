#include "stt_engine.h"
#include "whisper.h"
#include <iostream>

SttEngine::SttEngine() = default;

SttEngine::~SttEngine() {
    if (ctx_) whisper_free(ctx_);
}

bool SttEngine::loadModel(const std::string& model_path, int n_threads) {
    n_threads_ = n_threads;

    whisper_context_params cparams = whisper_context_default_params();
    // cparams.use_gpu = true; // uncomment if you built with GPU support and want STT offloaded too

    ctx_ = whisper_init_from_file_with_params(model_path.c_str(), cparams);
    if (!ctx_) {
        std::cerr << "[SttEngine] Failed to load whisper model at " << model_path << "\n";
        return false;
    }
    std::cout << "[SttEngine] Loaded whisper model: " << model_path << "\n";
    return true;
}

std::string SttEngine::transcribe(const std::vector<float>& samples) {
    if (!ctx_ || samples.empty()) return "";

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress   = false;
    wparams.print_special    = false;
    wparams.print_realtime   = false;
    wparams.print_timestamps = false;
    wparams.n_threads        = n_threads_;
    wparams.language         = "en"; // change or set to "auto" if needed
    wparams.translate        = false;
    wparams.no_context       = true;
    wparams.single_segment   = false;

    if (whisper_full(ctx_, wparams, samples.data(), (int)samples.size()) != 0) {
        std::cerr << "[SttEngine] whisper_full() failed\n";
        return "";
    }

    std::string result;
    const int n_segments = whisper_full_n_segments(ctx_);
    for (int i = 0; i < n_segments; ++i) {
        result += whisper_full_get_segment_text(ctx_, i);
    }
    return result;
}
