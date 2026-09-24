#include "llm_engine.h"

#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

LlmEngine::LlmEngine() {}

LlmEngine::~LlmEngine() {
    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    if (model_) {
        llama_free_model(model_);
        model_ = nullptr;
    }
    llama_backend_free();
}

bool LlmEngine::loadModel(const std::string& model_path, int n_ctx, int n_threads) {
    llama_backend_init();

    llama_model_params model_params = llama_model_default_params();
    model_ = llama_load_model_from_file(model_path.c_str(), model_params);
    if (!model_) {
        std::cerr << "[LlmEngine] Failed to load model: " << model_path << "\n";
        return false;
    }

    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx           = n_ctx;
    ctx_params.n_threads       = n_threads;
    ctx_params.n_threads_batch = n_threads;

    ctx_ = llama_new_context_with_model(model_, ctx_params);
    if (!ctx_) {
        std::cerr << "[LlmEngine] Failed to create context\n";
        return false;
    }

    n_ctx_ = n_ctx;
    return true;
}

void LlmEngine::resetHistory() {
    history_.clear();

    if (ctx_) {
        auto mem = llama_get_memory(ctx_);
        if (mem) {
            llama_memory_clear(mem, true);
        }
    }
}

std::string LlmEngine::applyChatTemplate(const std::string& system_prompt,
                                         const std::string& user_text) {
    // Generic instruct template. Replace with your model's exact chat template
    // if it uses different special tokens (e.g. Llama 3, Mistral, etc.).
    std::string prompt;

    prompt += "<|system|>\n" + system_prompt + "\n";

    for (const auto& turn : history_) {
        if (turn.first == "user") {
            prompt += "<|user|>\n" + turn.second + "\n";
        } else {
            prompt += "<|assistant|>\n" + turn.second + "\n";
        }
    }

    prompt += "<|user|>\n" + user_text + "\n<|assistant|>\n";
    return prompt;
}

std::string LlmEngine::generate(const std::string& system_prompt,
                                const std::string& user_text,
                                int max_tokens) {
    if (!model_ || !ctx_) return "";

    // ── 1. CRITICAL FIX: clear KV cache from the previous turn ──
    auto mem = llama_get_memory(ctx_);
    if (mem) {
        llama_memory_clear(mem, true);
    }

    // Ignore accidental whitespace-only input, but do NOT push it to history.
    std::string user = user_text;
    while (!user.empty() &&
           (user.back() == '\n' || user.back() == '\r' || user.back() == ' '))
        user.pop_back();
    if (user.empty()) return "";

    const llama_vocab* vocab = llama_model_get_vocab(model_);

    auto countTokens = [&](const std::string& text) -> int {
        int n = llama_tokenize(vocab, text.c_str(), (int)text.size(),
                               nullptr, 0, false, true);
        return n < 0 ? -n : n;
    };

    // ── Build the templated prompt, dropping oldest turns if it would not fit ──
    std::string full_prompt = applyChatTemplate(system_prompt, user);
    const int reserve = max_tokens + 64; // room for the reply + margin

    while (!history_.empty() && countTokens(full_prompt) + reserve > n_ctx_) {
        history_.erase(history_.begin()); // drop oldest exchange
        full_prompt = applyChatTemplate(system_prompt, user);
        std::cout << "[LlmEngine] Context nearly full – dropped oldest exchange "
                     "(history now " << history_.size() / 2 << " turns)\n";
    }

    // ── Tokenize (add_special=false: the template already carries BOS etc.) ──
    std::vector<llama_token> tokens(countTokens(full_prompt) + 8);
    int n_tokens = llama_tokenize(vocab,
                                  full_prompt.c_str(), (int)full_prompt.size(),
                                  tokens.data(), (int)tokens.size(),
                                  /*add_special*/ false, /*parse_special*/ true);
    if (n_tokens < 0) {
        tokens.resize(-n_tokens);
        n_tokens = llama_tokenize(vocab,
                                  full_prompt.c_str(), (int)full_prompt.size(),
                                  tokens.data(), (int)tokens.size(),
                                  false, true);
    }
    tokens.resize(n_tokens);

    llama_batch batch = llama_batch_get_one(tokens.data(), (int32_t)tokens.size());
    if (llama_decode(ctx_, batch) != 0) {
        std::cerr << "[LlmEngine] llama_decode failed on prompt\n";
        return "";
    }

    // ── Sampler: penalties kill repetition loops, mild temp keeps voice natural ──
    llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    llama_sampler* sampler = llama_sampler_chain_init(sparams);

    llama_sampler_chain_add(sampler,
        llama_sampler_init_penalties(64, 1.15f, 0.0f, 0.0f));
    llama_sampler_chain_add(sampler,
        llama_sampler_init_top_p(0.9f, 1));
    llama_sampler_chain_add(sampler,
        llama_sampler_init_temp(0.7f));

    const uint32_t seed = (uint32_t)std::chrono::high_resolution_clock::now()
                              .time_since_epoch().count();
    llama_sampler_chain_add(sampler,
        llama_sampler_init_dist(seed));

    std::string output;
    int generated = 0;
    llama_token new_token = -1;

    while (generated < max_tokens) {
        new_token = llama_sampler_sample(sampler, ctx_, -1);
        if (llama_vocab_is_eog(vocab, new_token)) break;
        llama_sampler_accept(sampler, new_token);

        char buf[256];
        int n = llama_token_to_piece(vocab, new_token,
                                     buf, sizeof(buf), 0, true);
        if (n > 0) output.append(buf, n);

        llama_batch next_batch = llama_batch_get_one(&new_token, 1);
        if (llama_decode(ctx_, next_batch) != 0) {
            std::cerr << "[LlmEngine] llama_decode failed during generation\n";
            break;
        }
        ++generated;
    }
    llama_sampler_free(sampler);

    // Strip trailing whitespace/newlines – game engines display this raw
    while (!output.empty() &&
           (output.back() == '\n' || output.back() == '\r' ||
            output.back() == ' '  || output.back() == '\t'))
        output.pop_back();

    // ── Remember the exchange so the next turn has full context ──
    history_.push_back({"user", user});
    history_.push_back({"assistant", output});

    return output;
}
