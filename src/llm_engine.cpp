#include "llm_engine.h"
#include "llama.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <fstream>
#include <thread>
#include <chrono>
#include <algorithm> // Required for std::transform
#include <cctype>    // Required for std::toupper

LlmEngine::LlmEngine() = default;

LlmEngine::~LlmEngine() {
    if (ctx_) llama_free(ctx_);
    if (model_) llama_model_free(model_);
}

// Helper function to capitalize all alphabetical characters in a string
static std::string toUppercase(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
        return std::toupper(c);
    });
    return str;
}

bool LlmEngine::loadModel(const std::string& model_path, int n_gpu_layers, int n_ctx) {
    n_ctx_ = n_ctx;

    llama_backend_init();

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = n_gpu_layers;

    model_ = llama_model_load_from_file(model_path.c_str(), mparams);
    if (!model_) {
        std::cerr << "[LlmEngine] Failed to load model at " << model_path << "\n";
        return false;
    }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = n_ctx_;

    // Increased from 512 to 2048 to prevent crashes with large system prompt sheets
    cparams.n_batch = 2048;

    ctx_ = llama_init_from_model(model_, cparams);
    if (!ctx_) {
        std::cerr << "[LlmEngine] Failed to create llama context\n";
        return false;
    }

    std::cout << "[LlmEngine] Loaded LLM: " << model_path
              << " (n_gpu_layers=" << n_gpu_layers << ")\n";
    return true;
}

std::string LlmEngine::generate(const std::string& system_prompt,
                                 const std::string& user_text,
                                 int max_tokens) {
    if (!model_ || !ctx_) return "";

    // Clear the memory contents (KV cache) using the modern Memory API.
    auto mem = llama_get_memory(ctx_);
    if (mem) {
        llama_memory_clear(mem, true);
    }

    const llama_vocab* vocab = llama_model_get_vocab(model_);

    // --- Qwen: Official ChatML Template Format ---
    std::string full_prompt =
        "<|im_start|>system\n" + system_prompt + "<|im_end|>\n"
        "<|im_start|>user\n" + user_text + "<|im_end|>\n"
        "<|im_start|>assistant\n";

    // --- Tokenize ---
    int n_tokens_max = (int)full_prompt.size() + 16;
    std::vector<llama_token> tokens(n_tokens_max);
    int n_tokens = llama_tokenize(vocab, full_prompt.c_str(), (int32_t)full_prompt.size(),
                                   tokens.data(), n_tokens_max, /*add_special*/ true, /*parse_special*/ true);
    if (n_tokens < 0) {
        tokens.resize(-n_tokens);
        n_tokens = llama_tokenize(vocab, full_prompt.c_str(), (int32_t)full_prompt.size(),
                                   tokens.data(), (int32_t)tokens.size(), true, true);
    }
    tokens.resize(n_tokens);

    llama_batch batch = llama_batch_get_one(tokens.data(), (int32_t)tokens.size());
    if (llama_decode(ctx_, batch) != 0) {
        std::cerr << "[LlmEngine] llama_decode failed on prompt\n";
        return "";
    }

    // --- Simple greedy-ish sampler chain ---
    llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    llama_sampler* sampler = llama_sampler_chain_init(sparams);
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.8f));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(0.9f, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    std::string output;
    int generated = 0;
    llama_token new_token = -1;

    while (generated < max_tokens) {
        new_token = llama_sampler_sample(sampler, ctx_, -1);

        if (llama_vocab_is_eog(vocab, new_token)) break;

        // Feed chosen token back to update sampler state
        llama_sampler_accept(sampler, new_token);

        char buf[256];
        int n = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0, true);
        if (n > 0) output.append(buf, n);

        llama_batch next_batch = llama_batch_get_one(&new_token, 1);
        if (llama_decode(ctx_, next_batch) != 0) {
            std::cerr << "[LlmEngine] llama_decode failed during generation\n";
            break;
        }
        ++generated;
    }

    llama_sampler_free(sampler);

    // Transform the final generated output string into all Capital Letters
    output = toUppercase(output);

    // --- Save output to last_response.txt with a 0.1-second delay ---
    std::string target_path = "game_state/last_response.txt";
    {
        std::ofstream test(target_path);
        if (!test.is_open()) {
            target_path = "last_response.txt";
        }
    }

    // Create/put an empty file (truncates the existing file content instantly) [3]
    {
        std::ofstream out(target_path, std::ios::trunc);
    }

    // Wait for 0.1 seconds (100 milliseconds) [5]
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Write the final response [3]
    {
        std::ofstream out(target_path, std::ios::trunc);
        if (out.is_open()) {
            out << output;
        }
    }

    return output;
}
