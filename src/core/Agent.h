// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#pragma once

#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>

#include "Model.h"
#include "Sampler.h"
#include "ChatHistory.h"
#include "Tool.h"

class ToolRegistry;

class Agent
{
public:
    using TokenCallback      = std::function<void(const std::string &)>;
    using CompletionCallback = std::function<void(const std::string &)>;
    using ErrorCallback      = std::function<void(const std::string &)>;
    using ToolResolveCallback = std::function<ToolResult(const ToolCall&, const ToolDefinition*)>;

    Agent() = default;
    ~Agent();

    Agent(const Agent &) = delete;
    Agent &operator=(const Agent &) = delete;

    static void init();
    static void shutdown();

    static ToolResult defaultRejectTool(const ToolCall &, const ToolDefinition *);

    bool loadModel(const std::string &path, const Model::Params &params = {});
    void unloadModel();
    bool isModelLoaded() const;

    Model        &model();
    Sampler      &sampler();
    ChatHistory  &history();

    void setSamplerParams(const Sampler::Params &params);
    Sampler::Params samplerParams() const;

    void setToolRegistry(ToolRegistry *registry);

    std::string chat(const std::string &userMessage,
                     ToolResolveCallback onToolResolve = defaultRejectTool);

    void chatStreaming(const std::string &userMessage,
                       TokenCallback      onToken,
                       CompletionCallback onComplete,
                       ToolResolveCallback onToolResolve,
                       ErrorCallback      onError);

    void cancel();
    bool isRunning() const;
    std::string lastError() const;
    int remainingContext() const;

private:
    void runChat(const std::string &userMessage,
                 TokenCallback      onToken,
                 ToolResolveCallback onToolResolve,
                 CompletionCallback onComplete,
                 ErrorCallback      onError);

    Model        m_model;
    Sampler      m_sampler;
    Sampler::Params m_samplerParams;
    ChatHistory  m_history;
    bool         m_running = false;
    std::string  m_lastError;
    ToolRegistry *m_toolRegistry = nullptr;
    std::atomic<bool> m_cancelled{false};
    std::atomic<int>  m_lastTokenCount{0};
    std::thread       m_worker;

    std::string  m_firstPassPrompt;
    std::string  m_toolContextSuffix;
    bool         m_bypassTemplate = false;
};
