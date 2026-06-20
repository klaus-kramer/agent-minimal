// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#pragma once

#include <cstdint>
#include <string>

#include "llama.h"

class Sampler
{
public:
    struct Params
    {
        float temperature     = 0.7f;
        float topP            = 0.9f;
        int   topK            = 40;
        float minP            = 0.05f;
        float repeatPenalty   = 1.1f;
        float freqPenalty     = 0.0f;
        float presencePenalty = 0.0f;
        int   penaltyLastN    = 64;
        int   maxTokens       = 4096;
        uint32_t seed         = 0;
    };

    Sampler() = default;
    ~Sampler();

    Sampler(const Sampler &) = delete;
    Sampler &operator=(const Sampler &) = delete;

    bool init(const llama_vocab *vocab, const Params &params = {});
    void reset(const llama_vocab *vocab);
    void free();

    llama_token sample(llama_context *ctx, int idx = -1);
    void accept(llama_token token);

    bool isValid() const;

private:
    llama_sampler *m_chain = nullptr;
    Params m_params;
};
