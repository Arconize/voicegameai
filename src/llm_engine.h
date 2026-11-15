#pragma once

#include <string>
#include <vector>
#include <utility>
#include <llama.h>

class LlmEngine {
public:
    LlmEngine();
    ~LlmEngine();

    bool loadModel(const std::string& model_path, int n_ctx = 4096, int n_threads = 4);
    void resetHistory();
    std::string generate(const std::string& system_prompt,
                         const std::string& user_text,
                         int max_tokens = 120,
                         float temperature = 0.7f);

private:
    std::string applyChatTemplate(const std::string& system_prompt,
                                  const std::string& user_text);

    llama_model*   model_ = nullptr;
    llama_context* ctx_   = nullptr;
    int            n_ctx_ = 4096;

    std::vector<std::pair<std::string, std::string>> history_;
};
