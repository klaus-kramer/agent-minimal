// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#pragma once

#include <string>
#include <optional>

namespace agent {

struct ModelConfig {
    std::string path;
    int contextSize = 8192;
    int batchSize = 512;
    int threads = 0;
    int gpuLayers = 99;
};

struct SamplerConfig {
    float temperature = 0.7f;
    float topP = 0.9f;
    int topK = 40;
    float minP = 0.05f;
    float repeatPenalty = 1.1f;
    float freqPenalty = 0.0f;
    float presencePenalty = 0.0f;
    int penaltyLastN = 64;
    int maxTokens = 4096;
    unsigned int seed = 0;
};

struct ToolConfig {
    bool enableFileTools = true;
    bool enableWebFetch = true;
    bool enableWebSearch = true;
    bool enableExecuteCommand = true;
    bool enableSearchPattern = true;
    bool enableSearchFiles = true;
    std::string allowedRoot;
};

struct AgentConfig {
    ModelConfig model;
    SamplerConfig sampler;
    ToolConfig tools;
    std::string systemPrompt;
};

std::optional<AgentConfig> loadConfig(const std::string& configPath);
bool saveConfigExample(const std::string& configPath);

}