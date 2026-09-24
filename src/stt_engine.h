#pragma once
#include <string>
#include <vector>

struct whisper_context; // fwd decl from whisper.cpp

class SttEngine {
public:
    SttEngine();
    ~SttEngine();

    // model_path: e.g. "models/ggml-small.en.bin"
    bool loadModel(const std::string& model_path, int n_threads = 4);

    // samples: mono float32 @ 16kHz (what AudioCapture produces)
    std::string transcribe(const std::vector<float>& samples);

private:
    whisper_context* ctx_ = nullptr;
    int n_threads_ = 4;
};
