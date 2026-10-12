#include "Config.h"
#include "JsonHelper.h"

#include <fstream>
#include <filesystem>
#include <iostream>

namespace agent {

namespace {

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return {};
    return std::string(std::istreambuf_iterator<char>(file), {});
}

int parseInt(const std::string& s, int defaultVal) {
    if (s.empty()) return defaultVal;
    try { return std::stoi(s); } catch (...) { return defaultVal; }
}

float parseFloat(const std::string& s, float defaultVal) {
    if (s.empty()) return defaultVal;
    try { return std::stof(s); } catch (...) { return defaultVal; }
}

unsigned int parseUint(const std::string& s, unsigned int defaultVal) {
    if (s.empty()) return defaultVal;
    try { return static_cast<unsigned int>(std::stoul(s)); } catch (...) { return defaultVal; }
}

bool parseBool(const std::string& s, bool defaultVal) {
    if (s.empty()) return defaultVal;
    std::string lower = s;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower == "true" || lower == "1" || lower == "yes";
}

std::string trimValue(const std::string& s) {
    size_t a = json_helper::skipWhitespace(s, 0);
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r'))
        --b;
    return s.substr(a, b - a);
}

// Parses a flat JSON object into key -> raw JSON value pairs. Values are kept
// verbatim so their JSON type (string/number/bool/object) is preserved for
// common_chat_templates_apply.
std::map<std::string, std::string> parseStringMap(const std::string& obj) {
    std::map<std::string, std::string> out;
    size_t pos = json_helper::skipWhitespace(obj, 0);
    if (pos >= obj.size() || obj[pos] != '{') return out;
    ++pos;
    while (pos < obj.size()) {
        pos = json_helper::skipWhitespace(obj, pos);
        if (pos >= obj.size() || obj[pos] == '}') break;
        if (obj[pos] == ',') { ++pos; continue; }
        if (obj[pos] != '"') break;
        size_t keyEnd = json_helper::findStringEnd(obj, pos);
        if (keyEnd == std::string::npos) break;
        std::string key = json_helper::unescapeJsonString(obj.substr(pos + 1, keyEnd - pos - 2));
        pos = json_helper::skipWhitespace(obj, keyEnd);
        if (pos >= obj.size() || obj[pos] != ':') break;
        pos = json_helper::skipWhitespace(obj, pos + 1);
        size_t valEnd = json_helper::findValueEnd(obj, pos);
        if (valEnd == std::string::npos) break;
        out[key] = trimValue(obj.substr(pos, valEnd - pos));
        pos = valEnd;
    }
    return out;
}

}

std::optional<AgentConfig> loadConfig(const std::string& configPath) {
    if (!std::filesystem::exists(configPath)) {
        std::cerr << "Config file not found: " << configPath << std::endl;
        return std::nullopt;
    }

    std::string content = readFile(configPath);
    if (content.empty()) {
        std::cerr << "Config file empty: " << configPath << std::endl;
        return std::nullopt;
    }

    AgentConfig cfg;

    cfg.model.path = json_helper::extractStringValue(content, "path");
    if (cfg.model.path.empty())
        cfg.model.path = json_helper::extractStringValue(content, "model");
    cfg.model.contextSize = parseInt(json_helper::extractRawValue(content, "contextSize"), cfg.model.contextSize);
    cfg.model.batchSize = parseInt(json_helper::extractRawValue(content, "batchSize"), cfg.model.batchSize);
    cfg.model.threads = parseInt(json_helper::extractRawValue(content, "threads"), cfg.model.threads);
    cfg.model.gpuLayers = parseInt(json_helper::extractRawValue(content, "gpuLayers"), cfg.model.gpuLayers);

    cfg.sampler.temperature = parseFloat(json_helper::extractRawValue(content, "temperature"), cfg.sampler.temperature);
    cfg.sampler.topP = parseFloat(json_helper::extractRawValue(content, "topP"), cfg.sampler.topP);
    cfg.sampler.topK = parseInt(json_helper::extractRawValue(content, "topK"), cfg.sampler.topK);
    cfg.sampler.minP = parseFloat(json_helper::extractRawValue(content, "minP"), cfg.sampler.minP);
    cfg.sampler.repeatPenalty = parseFloat(json_helper::extractRawValue(content, "repeatPenalty"), cfg.sampler.repeatPenalty);
    cfg.sampler.freqPenalty = parseFloat(json_helper::extractRawValue(content, "freqPenalty"), cfg.sampler.freqPenalty);
    cfg.sampler.presencePenalty = parseFloat(json_helper::extractRawValue(content, "presencePenalty"), cfg.sampler.presencePenalty);
    cfg.sampler.penaltyLastN = parseInt(json_helper::extractRawValue(content, "penaltyLastN"), cfg.sampler.penaltyLastN);
    cfg.sampler.maxTokens = parseInt(json_helper::extractRawValue(content, "maxTokens"), cfg.sampler.maxTokens);
    cfg.sampler.seed = parseUint(json_helper::extractRawValue(content, "seed"), cfg.sampler.seed);

    std::string toolsObj = json_helper::extractObjectValue(content, "tools");
    if (!toolsObj.empty()) {
        cfg.tools.enableFileTools = parseBool(json_helper::extractRawValue(toolsObj, "enableFileTools"), cfg.tools.enableFileTools);
        cfg.tools.enableWebFetch = parseBool(json_helper::extractRawValue(toolsObj, "enableWebFetch"), cfg.tools.enableWebFetch);
        cfg.tools.enableWebSearch = parseBool(json_helper::extractRawValue(toolsObj, "enableWebSearch"), cfg.tools.enableWebSearch);
        cfg.tools.enableExecuteCommand = parseBool(json_helper::extractRawValue(toolsObj, "enableExecuteCommand"), cfg.tools.enableExecuteCommand);
        cfg.tools.enableSearchPattern = parseBool(json_helper::extractRawValue(toolsObj, "enableSearchPattern"), cfg.tools.enableSearchPattern);
        cfg.tools.enableSearchFiles = parseBool(json_helper::extractRawValue(toolsObj, "enableSearchFiles"), cfg.tools.enableSearchFiles);
        cfg.tools.allowedRoot = json_helper::extractStringValue(toolsObj, "allowedRoot");
    }

    cfg.systemPrompt = json_helper::extractStringValue(content, "systemPrompt");

    cfg.templateKwargs = parseStringMap(json_helper::extractObjectValue(content, "templateKwargs"));
    std::string reasoningEffort = json_helper::extractStringValue(content, "reasoningEffort");
    if (!reasoningEffort.empty() && cfg.templateKwargs.find("reasoning_effort") == cfg.templateKwargs.end())
        cfg.templateKwargs["reasoning_effort"] = json_helper::toJsonLiteral(reasoningEffort);

    return cfg;
}

bool saveConfigExample(const std::string& configPath) {
    const char* example = R"({
  "model": {
    "path": "models/gemma-4-12B-it-Q4_K_M.gguf",
    "contextSize": 8192,
    "batchSize": 512,
    "threads": 0,
    "gpuLayers": 99
  },
  "sampler": {
    "temperature": 0.7,
    "topP": 0.9,
    "topK": 40,
    "minP": 0.05,
    "repeatPenalty": 1.1,
    "freqPenalty": 0.0,
    "presencePenalty": 0.0,
    "penaltyLastN": 64,
    "maxTokens": 4096,
    "seed": 0
  },
  "tools": {
    "enableFileTools": true,
    "enableWebFetch": true,
    "enableWebSearch": true,
    "enableExecuteCommand": true,
    "enableSearchPattern": true,
    "enableSearchFiles": true,
    "allowedRoot": ""
  },
  "systemPrompt": "You are a helpful AI assistant.",
    "templateKwargs": {}
})";

    std::ofstream file(configPath);
    if (!file) return false;
    file << example;
    return true;
}

}
