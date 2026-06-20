// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#include "Model.h"

#include <cstring>
#include <algorithm>

Model::~Model()
{
    freeResources();
}

Model::Model(Model &&other) noexcept
    : m_model(other.m_model)
    , m_ctx(other.m_ctx)
    , m_lastError(std::move(other.m_lastError))
{
    other.m_model = nullptr;
    other.m_ctx   = nullptr;
}

Model &Model::operator=(Model &&other) noexcept
{
    if (this != &other) {
        freeResources();
        m_model     = other.m_model;
        m_ctx       = other.m_ctx;
        m_lastError = std::move(other.m_lastError);
        other.m_model = nullptr;
        other.m_ctx   = nullptr;
    }
    return *this;
}

bool Model::load(const std::string &path, const Params &params)
{
    unload();

    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = params.gpuLayers;

    m_model = llama_model_load_from_file(path.c_str(), mp);
    if (!m_model) {
        m_lastError = "Failed to load model: " + path;
        return false;
    }

    llama_context_params cp = llama_context_default_params();
    cp.n_ctx    = params.contextSize;
    cp.n_batch  = params.batchSize;
    cp.n_threads = params.threads > 0
                       ? params.threads
                       : static_cast<int>(std::thread::hardware_concurrency());

    m_ctx = llama_init_from_model(m_model, cp);
    if (!m_ctx) {
        llama_free_model(m_model);
        m_model = nullptr;
        m_lastError = "Failed to create context";
        return false;
    }

    m_params = params;
    m_lastError.clear();
    return true;
}

void Model::unload()
{
    freeResources();
}

bool Model::isLoaded() const
{
    return m_model != nullptr && m_ctx != nullptr;
}

llama_model *Model::model() const
{
    return m_model;
}

llama_context *Model::context() const
{
    return m_ctx;
}

const llama_vocab *Model::vocab() const
{
    return m_model ? llama_model_get_vocab(m_model) : nullptr;
}

std::string Model::name() const
{
    if (!m_model) return {};
    char buf[256] = {};
    llama_model_desc(m_model, buf, sizeof(buf));
    return buf;
}

std::string Model::chatTemplate() const
{
    if (!m_model) return {};
    const char *t = llama_model_chat_template(m_model, nullptr);
    return t ? std::string(t) : std::string();
}

int Model::contextSize() const
{
    return m_ctx ? llama_n_ctx(m_ctx) : 0;
}

int Model::batchSize() const
{
    return m_params.batchSize;
}

uint64_t Model::parameterCount() const
{
    return m_model ? llama_model_n_params(m_model) : 0;
}

std::string Model::lastError() const
{
    return m_lastError;
}

void Model::freeResources()
{
    if (m_ctx) {
        llama_free(m_ctx);
        m_ctx = nullptr;
    }
    if (m_model) {
        llama_free_model(m_model);
        m_model = nullptr;
    }
}
