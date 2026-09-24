#include "llm_engine.h"
#include "llama.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <chrono>
#include <cstdint>

LlmEngine::LlmEngine() = default;

LlmEngine::~LlmEngine() {
    if (ctx_) llama_free(ctx_);
    if (model_) llama_model_free(model_);
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
    cparams.n_batch = n_ctx_ < 2048 ? n_ctx_ : 2048;

    ctx_ = llama_init_from_model(model_, cparams);
    if (!ctx_) {
        std::cerr << "[LlmEngine] Failed to create llama context\n";
        return false;
    }

    const char* tmpl = llama_model_get_chat_template(model_, 0);
    std::cout << "[LlmEngine] Loaded LLM: " << model_path
              << " (n_gpu_layers=" << n_gpu_layers
              << ", template=" << (tmpl ? tmpl : "none") << ")\n";
    return true;
}

void LlmEngine::resetHistory() {
    history_.clear();
    // Also drop the KV cache so the next session starts truly fresh.
    auto mem = llama_get_memory(ctx_);
    if (mem) llama_memory_clear(mem, true);
}

std::string LlmEngine::applyChatTemplate(const std::string& system_prompt,
                                         const std::string& user_text) {
    std::vector<llama_chat_message> msgs;
    msgs.reserve(history_.size() + 2);
    msgs.push_back({"system", system_prompt.c_str()});
    for (const auto& m : history_)
        msgs.push_back({m.first.c_str(), m.second.c_str()});
    msgs.push_back({"user", user_text.c_str()});

    const char* chat_template = llama_model_get_chat_template(model_, 0);

    // Size the buffer, then format (llama_chat_apply_template returns the
    // required length as a negative number if the buffer was too small).
    std::vector<char> buf(8192);
    int n = llama_chat_apply_template(
        nullptr, chat_template, msgs.data(), msgs.size(),
        /*add_generation_prompt*/ true, buf.data(), (int)buf.size());
    if (n < 0) {
        buf.resize(-n);
        n = llama_chat_apply_template(
            nullptr, chat_template, msgs.data(), msgs.size(),
            true, buf.data(), (int)buf.size());
    }
    if (n <= 0) {
        std::cerr << "[LlmEngine] chat template failed, falling back to plain prompt\n";
        return system_prompt + "\n\nPlayer: " + user_text + "\n" + "Character:";
    }
    return std::string(buf.data(), n);
}

std::string LlmEngine::generate(const std::string& system_prompt,
                                const std::string& user_text,
                                int max_tokens) {
    if (!model_ || !ctx_) return "";

    // Ignore accidental whitespace-only input, but DON'T push it to history.
    std::string user = user_text;
    while (!user.empty() && (user.back() == '\n' || user.back() == '\r' || user.back() == ' '))
        user.pop_back();
    if (user.empty()) return "";

    const llama_vocab* vocab = llama_model_get_vocab(model_);

    auto countTokens = [&](const std::string& text) -> int {
        int n = llama_tokenize(vocab, text.c_str(), (int)text.size(),
                               nullptr, 0, false, true);
        return n < 0 ? -n : n;
    };

    // --- Build the templated prompt, dropping oldest turns if it would not fit ---
    std::string full_prompt = applyChatTemplate(system_prompt, user);
    const int reserve = max_tokens + 64; // room for the reply + margin
    while (!history_.empty() &&
           countTokens(full_prompt) + reserve > n_ctx_) {
        history_.erase(history_.begin()); // drop oldest exchange
        full_prompt = applyChatTemplate(system_prompt, user);
        std::cout << "[LlmEngine] Context nearly full - dropped oldest exchange (history now "
                  << history_.size() / 2 << " turns)\n";
    }

    // --- Tokenize (add_special=false: the template already carries BOS etc.) ---
    std::vector<llama_token> tokens(countTokens(full_prompt) + 8);
    int n_tokens = llama_tokenize(vocab, full_prompt.c_str(), (int)full_prompt.size(),
                                  tokens.data(), (int)tokens.size(),
                                  /*add_special*/ false, /*parse_special*/ true);
    if (n_tokens < 0) {
        tokens.resize(-n_tokens);
        n_tokens = llama_tokenize(vocab, full_prompt.c_str(), (int)full_prompt.size(),
                                  tokens.data(), (int)tokens.size(), false, true);
    }
    tokens.resize(n_tokens);

    llama_batch batch = llama_batch_get_one(tokens.data(), (int32_t)tokens.size());
    if (llama_decode(ctx_, batch) != 0) {
        std::cerr << "[LlmEngine] llama_decode failed on prompt\n";
        return "";
    }

    // --- Sampler: penalties kill repetition loops, mild temp keeps voice natural ---
    llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    llama_sampler* sampler = llama_sampler_chain_init(sparams);
    llama_sampler_chain_add(sampler, llama_sampler_init_penalties(64, 1.15f, 0.0f, 0.0f));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(0.9f, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.7f));
    const uint32_t seed = (uint32_t)std::chrono::high_resolution_clock::now()
                              .time_since_epoch().count();
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(seed));

    std::string output;
    int generated = 0;
    llama_token new_token = -1;

    while (generated < max_tokens) {
        new_token = llama_sampler_sample(sampler, ctx_, -1);
        if (llama_vocab_is_eog(vocab, new_token)) break;

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

    // Strip trailing whitespace/newlines - game engines display this raw
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r' ||
                               output.back() == ' ' || output.back() == '\t'))
        output.pop_back();

    // --- Remember the exchange so the next turn has full context ---
    history_.push_back({"user", user});
    history_.push_back({"assistant", output});

    return output;
}
