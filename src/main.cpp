// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <filesystem>
#include <cstring>
#include <sstream>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/Agent.h"
#include "core/ToolRegistry.h"
#include "core/Config.h"
#include "tools/FileTool.h"
#include "tools/WebFetchTool.h"
#include "tools/WebSearchTool.h"
#include "tools/ExecuteCommandTool.h"
#include "tools/SearchPatternTool.h"
#include "tools/SearchFilesTool.h"

static void printHelp(const char *argv0)
{
    std::cerr
        << "agent_minimal v0.1.0  -  LLM Chat based on llama.cpp\n"
        << "\n"
        << "Usage:\n"
        << "  " << argv0 << " -m MODEL [options]\n"
        << "  " << argv0 << " --config CONFIG_FILE [options]\n"
        << "\n"
        << "Options:\n"
        << "  --config PATH          Path to JSON config file\n"
        << "  -m, --model PATH       Path to .gguf file (or filename in models/)\n"
        << "  -t, --temp VALUE       Temperature (default: 0.7)\n"
        << "  -c, --ctx-size N       Context size (default: 8192)\n"
        << "  -n, --max-tokens N     Max tokens to generate (default: 4096)\n"
        << "  -s, --system TEXT      System prompt\n"
        << "  -ngl, --gpu-layers N   GPU layers (default: 99)\n"

        << "  -l, --list             List models in models/\n"
        << "  -h, --help             Show this help\n"
        << "\n"
        << "Interactive commands:\n"
        << "  /exit, /quit           Exit the program\n"
        << "  /reset, /clear         Clear conversation history\n"
        << "  /system <text>         Set system prompt\n"
        << "  /params                Show current parameters\n"
        << "  /temp <value>          Change temperature\n"
        << "  /help                  Show command help\n"
        << std::endl;
}

static void listModels()
{
    const std::string dir = "models";
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
        std::cout << "  (directory 'models/' not found)\n";
        return;
    }

    std::cout << "Available models in models/:\n";
    int count = 0;
    for (auto &entry : std::filesystem::directory_iterator(dir)) {
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".gguf") {
            std::cout << "  " << entry.path().filename().string() << "\n";
            ++count;
        }
    }
    if (count == 0)
        std::cout << "  (no .gguf files found)\n";
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    Agent::init();

    std::string modelPath;
    Sampler::Params sparams;
    Model::Params   mparams;
    std::string systemPrompt;
    bool listOnly = false;
    std::string configPath;

    auto usage = [&]() { printHelp(argv[0]); };

    // First pass: check for --config
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config") {
            if (++i >= argc) { std::cerr << "Error: --config expects a value\n"; exit(1); }
            configPath = argv[i];
        }
    }

    // Load config file if provided
    agent::AgentConfig config;
    if (!configPath.empty()) {
        auto optConfig = agent::loadConfig(configPath);
        if (!optConfig) {
            std::cerr << "Failed to load config: " << configPath << "\n";
            Agent::shutdown();
            return 1;
        }
        config = *optConfig;
        // Apply config as defaults
        modelPath = config.model.path;
        mparams.contextSize = config.model.contextSize;
        mparams.batchSize = config.model.batchSize;
        mparams.threads = config.model.threads;
        mparams.gpuLayers = config.model.gpuLayers;
        sparams.temperature = config.sampler.temperature;
        sparams.topP = config.sampler.topP;
        sparams.topK = config.sampler.topK;
        sparams.minP = config.sampler.minP;
        sparams.repeatPenalty = config.sampler.repeatPenalty;
        sparams.freqPenalty = config.sampler.freqPenalty;
        sparams.presencePenalty = config.sampler.presencePenalty;
        sparams.penaltyLastN = config.sampler.penaltyLastN;
        sparams.maxTokens = config.sampler.maxTokens;
        sparams.seed = config.sampler.seed;
        systemPrompt = config.systemPrompt;
    }

    // Second pass: CLI args override config
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (++i >= argc) { std::cerr << "Error: " << arg << " expects a value\n"; exit(1); }
            return argv[i];
        };

        if (arg == "--config") {
            next(); // skip value, already processed
        } else if (arg == "-m" || arg == "--model")        modelPath = next();
        else if (arg == "-t" || arg == "--temp")    sparams.temperature = std::stof(next());
        else if (arg == "-c" || arg == "--ctx-size") mparams.contextSize = std::stoi(next());
        else if (arg == "-n" || arg == "--max-tokens") sparams.maxTokens = std::stoi(next());
        else if (arg == "-s" || arg == "--system")  systemPrompt = next();
        else if (arg == "-ngl" || arg == "--gpu-layers") mparams.gpuLayers = std::stoi(next());
        else if (arg == "-l" || arg == "--list")    listOnly = true;
        else if (arg == "-h" || arg == "--help")    { usage(); Agent::shutdown(); return 0; }
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            usage();
            return 1;
        }
    }

    if (listOnly) {
        listModels();
        Agent::shutdown();
        return 0;
    }

    if (modelPath.empty()) {
        std::cerr << "No model specified. Available models:\n";
        listModels();
        std::cerr << "\nUsage: " << argv[0] << " -m MODEL [-t 0.7] [--config CONFIG]\n";
        Agent::shutdown();
        return 1;
    }

    if (modelPath.find('/') == std::string::npos && modelPath.find('\\') == std::string::npos) {
        std::string candidate = "models/" + modelPath;
        if (std::filesystem::exists(candidate))
            modelPath = candidate;
    }

    ToolRegistry toolRegistry;
    if (!configPath.empty()) {
        if (config.tools.enableFileTools) registerFileTools(toolRegistry);
        if (config.tools.enableWebFetch) registerWebFetchTool(toolRegistry);
        if (config.tools.enableWebSearch) registerWebSearchTool(toolRegistry);
        if (config.tools.enableExecuteCommand) registerExecuteCommandTool(toolRegistry);
        if (config.tools.enableSearchPattern) registerSearchPatternTool(toolRegistry);
        if (config.tools.enableSearchFiles) registerSearchFilesTool(toolRegistry);
        if (!config.tools.allowedRoot.empty())
            setAllowedRoot(config.tools.allowedRoot);
    } else {
        registerFileTools(toolRegistry);
        registerWebFetchTool(toolRegistry);
        registerWebSearchTool(toolRegistry);
        registerExecuteCommandTool(toolRegistry);
        registerSearchPatternTool(toolRegistry);
        registerSearchFilesTool(toolRegistry);
    }

    Agent agent;
    agent.setSamplerParams(sparams);
    agent.setToolRegistry(&toolRegistry);
    if (!systemPrompt.empty())
        agent.history().setSystemPrompt(systemPrompt);

    std::cout << "Loading model: " << modelPath << " ..." << std::endl;

    if (!agent.loadModel(modelPath, mparams)) {
        std::cerr << "ERROR: " << agent.lastError() << std::endl;
        Agent::shutdown();
        return 1;
    }

    std::cout << "Model loaded: " << agent.model().name()
              << " (" << (agent.model().parameterCount() / 1e9) << "B parameters)"
              << std::endl;
    std::cout << "Context size: " << agent.model().contextSize() << " tokens"
              << std::endl;

    std::cout << "agent-minimal ready" << std::endl;

    std::cout << "Type /help for commands.\n" << std::endl;

    std::string line;
    while (true) {
        int rctx = agent.remainingContext();
        std::cout << rctx << "> " << std::flush;

        if (!std::getline(std::cin, line))
            break;

        if (line.empty())
            continue;

        if (line[0] == '/') {
            auto cmd = line.substr(1);
            if (cmd == "exit" || cmd == "quit") {
                break;
            } else if (cmd == "reset" || cmd == "clear") {
                agent.history().clear();
                std::cout << "History cleared.\n";
            } else if (cmd == "system" || cmd.rfind("system ", 0) == 0) {
                std::string sp = cmd.size() > 7 ? cmd.substr(7) : "";
                if (!sp.empty() && sp[0] == ' ') sp = sp.substr(1);
                agent.history().setSystemPrompt(sp);
                std::cout << "System prompt set.\n";
            } else if (cmd == "params") {
                auto sp = agent.samplerParams();
                std::cout << "Temperature:     " << sp.temperature << "\n"
                          << "Max Tokens:      " << sp.maxTokens << "\n"
                          << "Top-P:           " << sp.topP << "\n"
                          << "Top-K:           " << sp.topK << "\n"
                          << "Min-P:           " << sp.minP << "\n"
                          << "Repeat Penalty:  " << sp.repeatPenalty << "\n"
                          << "GPU Layers:      " << mparams.gpuLayers << "\n"
                          << "Ctx Size:        " << mparams.contextSize << "\n"
                          << "System Prompt:   " << agent.history().systemPrompt() << "\n";
            } else if (cmd == "temp" || cmd.rfind("temp ", 0) == 0) {
                auto sp = agent.samplerParams();
                std::string val = cmd.size() > 5 ? cmd.substr(5) : "";
                if (!val.empty() && val[0] == ' ') val = val.substr(1);
                if (!val.empty()) {
                    try {
                        sp.temperature = std::stof(val);
                        agent.setSamplerParams(sp);
                        std::cout << "Temperature set to " << sp.temperature << "\n";
                    } catch (...) {
                        std::cerr << "Invalid value.\n";
                    }
                } else {
                    std::cout << "Temperature: " << sp.temperature << "\n";
                }
            } else if (cmd == "tools") {
                std::cout << "Available AI assistant tools:\n";
                toolRegistry.listTools(std::cout);
            } else if (cmd == "help") {
                std::cout
                    << "Commands:\n"
                    << "  /exit              Exit the program\n"
                    << "  /reset             Clear conversation history\n"
                    << "  /system <text>     Set system prompt\n"
                    << "  /params            Show current parameters\n"
                    << "  /temp [value]      Show/change temperature\n"
                    << "  /tools             List available AI tools\n"
                    << "  /help              Show this help\n";
            } else {
                std::cerr << "Unknown command: " << cmd << "  (/help for help)\n";
            }
            continue;
        }

        std::atomic<bool> done(false);
        std::string errorMsg;

        std::string outputBuffer;

        agent.chatStreaming(line,
            [&](const std::string &token) {
                outputBuffer += token;

                if (token.find('\n') != std::string::npos || outputBuffer.size() > 120) {
                    std::cout << outputBuffer << std::flush;
                    outputBuffer.clear();
                }
            },
            [&](const std::string &) {
                if (!outputBuffer.empty()) {
                    std::cout << outputBuffer << std::flush;
                    outputBuffer.clear();
                }
                done = true;
            },
            [&](const ToolCall &call, const ToolDefinition *def) -> ToolResult {
                if (!outputBuffer.empty()) {
                    std::cout << outputBuffer << std::flush;
                    outputBuffer.clear();
                }
                bool needsConfirm = (def != nullptr && def->requiresConfirmation);
                if (needsConfirm) {
                    std::cout << "\n\n[Tool request] " << call.name
                              << " with args: " << call.rawArguments << "\n"
                              << "Allow? (y/N): " << std::flush;
                    std::string input;
                    std::getline(std::cin, input);
                    if (input != "y" && input != "Y") {
                        std::cout << "[Denied]\n";
                        return ToolResult{false, "Execution denied by user."};
                    }
                }
                std::cout << "[Executing " << call.name << " ...]\n";
                return toolRegistry.executeTool(call);
            },
            [&](const std::string &err) {
                if (!outputBuffer.empty()) {
                    std::cout << outputBuffer << std::flush;
                    outputBuffer.clear();
                }
                errorMsg = err;
                done = true;
            }
        );

        while (!done)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        if (!errorMsg.empty())
            std::cerr << "\nError: " << errorMsg << std::endl;

        std::cout << std::endl;
    }

    std::cout << "\nGoodbye!\n";
    Agent::shutdown();
    return 0;
}
