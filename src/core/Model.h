// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#pragma once

#include <string>
#include <thread>

#include "llama.h"

class Model
{
public:
    struct Params
    {
        int contextSize = 8192;
        int batchSize  = 512;
        int threads    = 0;
        int gpuLayers  = 99;
    };

    Model() = default;
    ~Model();

    Model(const Model &) = delete;
    Model &operator=(const Model &) = delete;
    Model(Model &&other) noexcept;
    Model &operator=(Model &&other) noexcept;

    bool load(const std::string &path, const Params &params = {});
    void unload();
    bool isLoaded() const;

    llama_model   *model()   const;
    llama_context *context() const;
    const llama_vocab *vocab() const;

    std::string name()        const;
    std::string chatTemplate() const;
    int         contextSize()  const;
    int         batchSize()    const;
    uint64_t    parameterCount() const;
    std::string lastError()    const;

private:
    void freeResources();

    llama_model   *m_model = nullptr;
    llama_context *m_ctx   = nullptr;
    Params         m_params;
    std::string    m_lastError;
};
