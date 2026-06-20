// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#include "Sampler.h"

#include <random>

Sampler::~Sampler()
{
    free();
}

bool Sampler::init(const llama_vocab *vocab, const Params &params)
{
    free();
    m_params = params;

    uint32_t seed = params.seed;
    if (seed == 0)
        seed = static_cast<uint32_t>(std::random_device{}());

    auto sparams = llama_sampler_chain_default_params();
    m_chain = llama_sampler_chain_init(sparams);

    llama_sampler_chain_add(m_chain, llama_sampler_init_top_k(params.topK));
    llama_sampler_chain_add(m_chain, llama_sampler_init_top_p(params.topP, 1));
    llama_sampler_chain_add(m_chain, llama_sampler_init_min_p(params.minP, 1));
    llama_sampler_chain_add(m_chain, llama_sampler_init_temp(params.temperature));

    if (params.repeatPenalty != 1.0f || params.freqPenalty != 0.0f || params.presencePenalty != 0.0f) {
        llama_sampler_chain_add(m_chain,
            llama_sampler_init_penalties(
                params.penaltyLastN,
                params.repeatPenalty,
                params.freqPenalty,
                params.presencePenalty));
    }

    llama_sampler_chain_add(m_chain, llama_sampler_init_dist(seed));

    return true;
}

void Sampler::reset(const llama_vocab *vocab)
{
    init(vocab, m_params);
}

void Sampler::free()
{
    if (m_chain) {
        llama_sampler_free(m_chain);
        m_chain = nullptr;
    }
}

llama_token Sampler::sample(llama_context *ctx, int idx)
{
    return llama_sampler_sample(m_chain, ctx, idx);
}

void Sampler::accept(llama_token token)
{
    llama_sampler_accept(m_chain, token);
}

bool Sampler::isValid() const
{
    return m_chain != nullptr;
}
