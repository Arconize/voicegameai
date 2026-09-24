#pragma once
#include <string>

struct llama_model;   // fwd decl from llama.cpp
struct llama_context;

class LlmEngine {
public:
    LlmEngine();
    ~LlmEngine();

    // model_path: e.g. "models/llama-3.2-3b-instruct-q4_k_m.gguf"
    // n_gpu_layers: 0 = pure CPU
    bool loadModel(const std::string& model_path, int n_gpu_layers = 0, int n_ctx = 4096);

    // system_prompt: built from the active Persona
    // user_text: the transcribed player speech
    // max_tokens is set high by default to prevent length limits
    std::string generate(const std::string& system_prompt,
                          const std::string& user_text,
                          int max_tokens = 2048);

private:
    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    int n_ctx_ = 4096;
};
