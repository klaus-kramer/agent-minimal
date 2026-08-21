// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#include "Agent.h"
#include "ToolRegistry.h"
#include "JsonHelper.h"

#include <mutex>
#include <thread>
#include <vector>
#include <chrono>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <unordered_set>

static std::mutex s_llamaMutex;
static int s_llamaRefCount = 0;

static constexpr size_t MAX_TOOL_RESULT_CHARS = 16384;

static void agentLogCallback(ggml_log_level level, const char * text, void * /*user_data*/)
{
    if (text && level >= GGML_LOG_LEVEL_INFO)
        std::fputs(text, stderr);
}

void Agent::init()
{
    std::lock_guard<std::mutex> lock(s_llamaMutex);
    if (s_llamaRefCount++ == 0) {
        llama_log_set(agentLogCallback, nullptr);
        llama_backend_init();
    }
}

void Agent::shutdown()
{
    std::lock_guard<std::mutex> lock(s_llamaMutex);
    if (--s_llamaRefCount == 0)
        llama_backend_free();
}

Agent::~Agent()
{
    m_cancelled = true;
    if (m_worker.joinable())
        m_worker.join();
}

ToolResult Agent::defaultRejectTool(const ToolCall &, const ToolDefinition *)
{
    return {false, "Tool execution denied by default handler."};
}

bool Agent::loadModel(const std::string &path, const Model::Params &params)
{
    unloadModel();
    if (!m_model.load(path, params)) {
        m_lastError = m_model.lastError();
        return false;
    }

    const auto *vocab = m_model.vocab();
    m_sampler.init(vocab, m_samplerParams);

    m_lastError.clear();
    return true;
}

void Agent::unloadModel()
{
    m_sampler.free();
    m_history.clear();
    m_model.unload();
}

bool Agent::isModelLoaded() const
{
    return m_model.isLoaded();
}

Model &Agent::model()
{
    return m_model;
}

Sampler &Agent::sampler()
{
    return m_sampler;
}

ChatHistory &Agent::history()
{
    return m_history;
}

void Agent::setSamplerParams(const Sampler::Params &params)
{
    m_samplerParams = params;
    if (m_model.isLoaded())
        m_sampler.init(m_model.vocab(), m_samplerParams);
}

Sampler::Params Agent::samplerParams() const
{
    return m_samplerParams;
}

void Agent::setThinkingEnabled(bool enabled)
{
    m_thinkingEnabled = enabled;
}

bool Agent::thinkingEnabled() const
{
    return m_thinkingEnabled;
}

bool Agent::isQwenFamily() const
{
    std::string name = m_model.name();
    for (auto &c : name)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return name.find("qwen") != std::string::npos;
}

void Agent::setToolRegistry(ToolRegistry *registry)
{
    m_toolRegistry = registry;
}

std::string Agent::chat(const std::string &userMessage,
                        ToolResolveCallback onToolResolve)
{
    if (!m_model.isLoaded()) {
        m_lastError = "No model loaded";
        return {};
    }

    std::string result;
    std::string error;

    runChat(userMessage,
        [&](const std::string &token) { result += token; },
        onToolResolve,
        [&](const std::string &full) { result = full; },
        [&](const std::string &err) { error = err; }
    );

    if (!error.empty())
        m_lastError = error;
    return result;
}

void Agent::chatStreaming(const std::string &userMessage,
                          TokenCallback onToken,
                          CompletionCallback onComplete,
                          ToolResolveCallback onToolResolve,
                          ErrorCallback onError)
{
    if (!m_model.isLoaded()) {
        if (onError) onError(m_lastError = "No model loaded");
        return;
    }
    if (m_running) {
        if (onError) onError(m_lastError = "Generation already in progress");
        return;
    }
    if (m_worker.joinable())
        m_worker.join();

    m_running = true;
    m_cancelled = false;

    m_worker = std::thread([this, userMessage, onToken, onComplete, onToolResolve, onError]() {
        runChat(userMessage, onToken, onToolResolve, onComplete, onError);
    });
}

void Agent::runChat(const std::string &userMessage,
                    TokenCallback onToken,
                    ToolResolveCallback onToolResolve,
                    CompletionCallback onComplete,
                    ErrorCallback onError)
{
    m_firstPassPrompt.clear();
    m_bypassTemplate = false;
    m_toolContextSuffix.clear();
    try {
        m_history.addMessage("user", userMessage);

        auto buildPrompt = [this]() -> std::string {
            std::string p = m_history.applyTemplate(m_model, true);
            if (!m_thinkingEnabled && isQwenFamily())
                p += "<think>\n\n</think>\n\n";
            return p;
        };

        if (!m_sampler.isValid())
            m_sampler.init(m_model.vocab(), m_samplerParams);

        bool toolsActive = m_toolRegistry && !m_toolRegistry->isEmpty();
        std::string savedSystem = m_history.systemPrompt();
        int toolIter = 0;
        const int MAX_TOOL_ITERS = 5;

        do {
            if (toolsActive) {
                std::string toolPrompt = m_toolRegistry->generateToolPrompt();
                if (!toolPrompt.empty()) {
                    std::string fullSystem = savedSystem;
                    if (!fullSystem.empty() && fullSystem.back() != '\n')
                        fullSystem += '\n';
                    fullSystem += toolPrompt;
                    m_history.setSystemPrompt(fullSystem);
                }
            }

            auto *ctx   = m_model.context();
            auto *model = m_model.model();
            auto *vocab = m_model.vocab();

            int ctxSize   = llama_n_ctx(ctx);
            int maxTokens = m_samplerParams.maxTokens;
            // Reserve at most a quarter of the context for generation output so
            // that tool results still have room in the prompt.
            int genReserve = std::min(maxTokens, std::max(64, ctxSize / 4));
            int maxPrompt  = ctxSize - genReserve - 32;
            if (maxPrompt < 32) maxPrompt = 32;

            std::string prompt;
            if (m_bypassTemplate && !m_firstPassPrompt.empty()) {
                prompt = m_firstPassPrompt + m_toolContextSuffix;
            } else {
                prompt = buildPrompt();
                if (m_firstPassPrompt.empty() && !prompt.empty()) {
                    m_firstPassPrompt = prompt;
                }
            }

            std::vector<llama_token> tokens(ctxSize);
            int nTokens = llama_tokenize(vocab,
                prompt.c_str(), static_cast<int>(prompt.size()),
                tokens.data(), static_cast<int>(tokens.size()),
                true, true);

            if (nTokens < 0) {
                if (m_bypassTemplate && !m_firstPassPrompt.empty()) {
                    // Shrink the tool context suffix (drop from the front) until
                    // the prompt fits, instead of silently discarding the tool
                    // result. The trailing model marker is preserved.
                    const std::string modelOpen = "\n<|turn>model\n";
                    while (nTokens < 0 && !m_toolContextSuffix.empty()) {
                        size_t cut = std::max<size_t>(1, m_toolContextSuffix.size() / 4);
                        m_toolContextSuffix.erase(0, cut);
                        prompt = m_firstPassPrompt + m_toolContextSuffix;
                        nTokens = llama_tokenize(vocab,
                            prompt.c_str(), static_cast<int>(prompt.size()),
                            tokens.data(), static_cast<int>(tokens.size()),
                            true, true);
                    }
                    if (!m_toolContextSuffix.empty() &&
                        (m_toolContextSuffix.size() < modelOpen.size() ||
                         m_toolContextSuffix.compare(m_toolContextSuffix.size() - modelOpen.size(),
                                                     modelOpen.size(), modelOpen) != 0)) {
                        m_toolContextSuffix += modelOpen;
                        prompt = m_firstPassPrompt + m_toolContextSuffix;
                        nTokens = llama_tokenize(vocab,
                            prompt.c_str(), static_cast<int>(prompt.size()),
                            tokens.data(), static_cast<int>(tokens.size()),
                            true, true);
                    }
                    if (nTokens < 0) {
                        m_history.trimToFit(m_model, maxPrompt);
                        prompt = buildPrompt();
                        m_firstPassPrompt = prompt;
                        m_toolContextSuffix.clear();
                        m_bypassTemplate = false;
                        nTokens = llama_tokenize(vocab,
                            prompt.c_str(), static_cast<int>(prompt.size()),
                            tokens.data(), static_cast<int>(tokens.size()),
                            true, true);
                    }
                    if (nTokens > 0)
                        std::cerr << "\n[Context near capacity - tool output was trimmed]\n";
                } else {
                    m_history.trimToFit(m_model, maxPrompt);
                    prompt = buildPrompt();
                    nTokens = llama_tokenize(vocab,
                        prompt.c_str(), static_cast<int>(prompt.size()),
                        tokens.data(), static_cast<int>(tokens.size()),
                        true, true);
                }
            }

            if (nTokens <= 0) {
                if (onError) onError(m_lastError = "Tokenization failed");
                m_running = false;
                return;
            }
            tokens.resize(nTokens);
            m_lastTokenCount = nTokens;

            llama_memory_clear(llama_get_memory(ctx), true);

            int batchSize = m_model.batchSize();
            for (int i = 0; i < nTokens; i += batchSize) {
                int nBatch = std::min(batchSize, nTokens - i);
                llama_batch batch = llama_batch_get_one(
                    tokens.data() + i, nBatch);
                if (llama_decode(ctx, batch) != 0) {
                    if (onError) onError(m_lastError = "Prompt decode failed");
                    m_running = false;
                    return;
                }
            }

            std::string modelName = m_model.name();
            for (auto &c : modelName)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            bool needsPreambleFilter = (modelName.find("gemma") != std::string::npos);
            enum PreambleState { SCANNING, DISCARDING, STREAMING };
            PreambleState pState = needsPreambleFilter ? SCANNING : STREAMING;
            std::string discardBuf;

            std::string response;
            std::string rawResponse;
            int generated = 0;
            bool suppressStream = false;
            int toolJsonDepth = 0;
            size_t toolScanPos = 0;

            while (generated < maxTokens && !m_cancelled) {
                llama_token token = m_sampler.sample(ctx, -1);

                if (llama_vocab_is_eog(vocab, token))
                    break;

                m_sampler.accept(token);

                char buf[16];
                int n = llama_token_to_piece(vocab, token, buf, sizeof(buf), 0, true);
                if (n > 0) {
                    std::string piece(buf, static_cast<size_t>(n));
                    rawResponse += piece;

                    bool isToolMarker = (piece == "<|tool_call>" || piece == "<tool_call|>" || piece == "<|tool_call|>");
                    if (isToolMarker) {
                        if (onToken) onToken("\n");
                    }

                    if (isToolMarker) {
                        // marker already handled above, just fall through to decode
                    } else if (pState == STREAMING) {
                        if (!suppressStream) {
                            size_t openPos = piece.find("<|channel>");
                            if (openPos != std::string::npos) {
                                pState = DISCARDING;
                                discardBuf = piece.substr(openPos + 10);
                                goto piece_handled;
                            }
                        }
                        if (!suppressStream) {
                            std::string check = response + piece;
                            if (check.size() < 300) {
                                auto fpos = check.find("\"function\":", toolScanPos);
                                if (fpos != std::string::npos && fpos < 300) {
                                    auto bracePos = check.rfind('{', fpos);
                                    if (bracePos != std::string::npos && bracePos >= toolScanPos && check.size() - bracePos < 150) {
                                        suppressStream = true;
                                        toolJsonDepth = 0;
                                        for (size_t i = bracePos; i < response.size(); ++i)
                                            if (response[i] == '{') ++toolJsonDepth;
                                            else if (response[i] == '}') --toolJsonDepth;
                                        if (onToken) onToken("\n[Calling tool...]\n");
                                    }
                                }
                                auto gemmaPos = check.find("call:", toolScanPos);
                                if (!suppressStream && gemmaPos != std::string::npos && gemmaPos < 300) {
                                    auto bracePos = check.find('{', gemmaPos);
                                    if (bracePos != std::string::npos && bracePos >= toolScanPos && bracePos - gemmaPos < 80) {
                                        suppressStream = true;
                                        toolJsonDepth = 0;
                                        for (size_t i = bracePos; i < response.size(); ++i)
                                            if (response[i] == '{') ++toolJsonDepth;
                                            else if (response[i] == '}') --toolJsonDepth;
                                        if (onToken) onToken("\n[Calling tool...]\n");
                                    }
                                }
                            }
                        }

                        if (suppressStream) {
                            for (char c : piece)
                                if (c == '{') ++toolJsonDepth;
                                else if (c == '}') --toolJsonDepth;
                            response += piece;
                            if (toolJsonDepth <= 0) {
                                suppressStream = false;
                                toolJsonDepth = 0;
                                toolScanPos = response.size();
                            }
                        }
                        if (!suppressStream) {
                            response += piece;
                            if (onToken) onToken(piece);
                        }
                    } else if (pState == SCANNING) {
                        discardBuf += piece;
                        auto markerPos = discardBuf.find("<channel|");
                        if (markerPos != std::string::npos) {
                            discardBuf = discardBuf.substr(markerPos + 9);
                            if (!discardBuf.empty() && discardBuf[0] == '>')
                                discardBuf = discardBuf.substr(1);
                            if (!discardBuf.empty()) {
                                response += discardBuf;
                                if (onToken) onToken(discardBuf);
                            }
                            discardBuf.clear();
                            pState = STREAMING;
                        } else if (discardBuf.find("<|channel>") == std::string::npos) {
                            if (discardBuf.size() > 128) {
                                pState = STREAMING;
                                response = discardBuf;
                                if (onToken) onToken(discardBuf);
                                discardBuf.clear();
                            }
                        }
                    } else if (pState == DISCARDING) {
                        discardBuf += piece;
                        size_t endPos = discardBuf.find("<channel|");
                        if (endPos != std::string::npos) {
                            std::string rest = discardBuf.substr(endPos + 9);
                            if (!rest.empty() && rest[0] == '>')
                                rest = rest.substr(1);
                            discardBuf.clear();
                            pState = STREAMING;
                            if (!rest.empty()) {
                                response += rest;
                                if (onToken) onToken(rest);
                            }
                        }
                    }
                    piece_handled: ;
                }

                llama_batch batch = llama_batch_get_one(&token, 1);
                if (llama_decode(ctx, batch) != 0) {
                    break;
                }

                ++generated;
            }

            if (pState == SCANNING && discardBuf.find("<|channel>") == std::string::npos) {
                response += discardBuf;
            }
            discardBuf.clear();

            m_lastTokenCount = nTokens + generated;

            if (m_cancelled) {
                m_running = false;
                return;
            }

            auto stripMarkers = [](std::string &s) {
                auto removeAll = [&](const std::string &sub) {
                    size_t p;
                    while ((p = s.find(sub)) != std::string::npos)
                        s.erase(p, sub.size());
                };
                removeAll("<start_of_turn>");
                removeAll("</start_of_turn>");
                removeAll("<end_of_turn>");
                removeAll("</end_of_turn>");
                removeAll("<bos>");
                removeAll("<eos>");

                removeAll("<|tool_call|>");
                removeAll("<|tool_call>");
                removeAll("<tool_call|>");
                removeAll("<|channel>");
                removeAll("<channel|>");
            };
            stripMarkers(response);

            bool toolCalled = false;
            if (toolsActive && toolIter < MAX_TOOL_ITERS) {
                std::string remaining = response;
                std::unordered_set<std::string> seenCalls;
                while (toolIter < MAX_TOOL_ITERS) {
                    ToolCall call = m_toolRegistry->parseToolCall(remaining);
                    if (call.name.empty()) break;

                    std::string callKey = call.name + "|" + call.rawArguments;

                    if (!seenCalls.insert(callKey).second) {
                        bool removed = false;
                        std::string jsonKey = "\"function\": \"" + call.name + "\"";
                        size_t pos = remaining.find(jsonKey);
                        if (pos != std::string::npos) {
                            size_t openBrace = remaining.rfind('{', pos);
                            if (openBrace != std::string::npos) {
                                size_t closeBrace = json_helper::findValueEnd(remaining, openBrace);
                                if (closeBrace != std::string::npos) {
                                    remaining = remaining.substr(0, openBrace) + remaining.substr(closeBrace);
                                    removed = true;
                                }
                            }
                        }
                        if (!removed) {
                            std::string gemmaKey = "call:" + call.name;
                            pos = remaining.find(gemmaKey);
                            if (pos != std::string::npos) {
                                size_t openBrace = remaining.find('{', pos);
                                if (openBrace != std::string::npos) {
                                    size_t closeBrace = json_helper::findValueEnd(remaining, openBrace);
                                    if (closeBrace != std::string::npos) {
                                        remaining = remaining.substr(0, pos) + remaining.substr(closeBrace);
                                    }
                                }
                            }
                        }
                        if (!removed) break;
                    }

                    if (call.name == "thought") {
                        bool removed = false;
                        std::string gemmaKey = "call:" + call.name;
                        size_t gp = remaining.find(gemmaKey);
                        if (gp != std::string::npos) {
                            size_t openBrace = remaining.find('{', gp);
                            if (openBrace != std::string::npos) {
                                size_t closeBrace = json_helper::findValueEnd(remaining, openBrace);
                                if (closeBrace != std::string::npos) {
                                    remaining = remaining.substr(0, gp) + remaining.substr(closeBrace);
                                    removed = true;
                                }
                            }
                        }
                        if (!removed) break;
                        continue;
                    }

                    const ToolDefinition *def = m_toolRegistry->findTool(call.name);
                    if (!def) break;

                    ToolResult result = onToolResolve(call, def);

                    std::string toolOutput = result.output;
                    if (toolOutput.size() > MAX_TOOL_RESULT_CHARS) {
                        toolOutput.resize(MAX_TOOL_RESULT_CHARS);
                        toolOutput += "\n...(tool output truncated for context at "
                            + std::to_string(MAX_TOOL_RESULT_CHARS) + " chars)";
                    }

                    std::string asstText = rawResponse;
                    {
                        size_t p = 0;
                        while ((p = asstText.find("<|channel>")) != std::string::npos) {
                            size_t close = asstText.find("<channel|>", p);
                            if (close == std::string::npos) {
                                asstText.erase(p);
                                break;
                            }
                            asstText.erase(p, close - p + 9);
                        }
                    }

                    std::string toolResultStr
                        = asstText
                        + "<turn|>\n"
                        + "<|turn>tool\n"
                        + toolOutput
                        + "<turn|>\n"
                        + "<|turn>model\n"
                        + "Tool call executed. If you have enough information, provide your final answer now. "
                          "Call more tools only if you still need additional information.\n";
                    m_toolContextSuffix += toolResultStr;
                    m_bypassTemplate = true;
                    m_sampler.reset(vocab);
                    toolCalled = true;
                    toolIter++;

                    bool removed = false;
                    std::string jsonKey = "\"function\": \"" + call.name + "\"";
                    size_t pos = remaining.find(jsonKey);
                    if (pos != std::string::npos) {
                        size_t openBrace = remaining.rfind('{', pos);
                        if (openBrace != std::string::npos) {
                            size_t closeBrace = json_helper::findValueEnd(remaining, openBrace);
                            if (closeBrace != std::string::npos) {
                                remaining = remaining.substr(0, openBrace) + remaining.substr(closeBrace);
                                removed = true;
                            }
                        }
                    }
                    if (!removed) {
                        std::string gemmaKey = "call:" + call.name;
                        pos = remaining.find(gemmaKey);
                        if (pos != std::string::npos) {
                            size_t openBrace = remaining.find('{', pos);
                            if (openBrace != std::string::npos) {
                                size_t closeBrace = json_helper::findValueEnd(remaining, openBrace);
                                if (closeBrace != std::string::npos) {
                                    remaining = remaining.substr(0, pos) + remaining.substr(closeBrace);
                                    removed = true;
                                }
                            }
                        }
                    }
                    if (!removed) break;
                }
            }

            if (toolCalled) {
                continue;
            }

            if (m_cancelled) {
                m_running = false;
                return;
            }

            if (m_bypassTemplate) {
                std::string fullInteraction = m_toolContextSuffix + response;
                m_history.addMessage("assistant", fullInteraction);
            } else {
                m_history.addMessage("assistant", response);
            }
            m_history.setSystemPrompt(savedSystem);
            m_bypassTemplate = false;
            m_toolContextSuffix.clear();

            if (onComplete) onComplete(response);
            break;

        } while (true);

    } catch (const std::exception &e) {
        if (onError) onError(m_lastError = e.what());
    }

    m_running = false;
}

void Agent::cancel()
{
    m_cancelled = true;
}

bool Agent::isRunning() const
{
    return m_running;
}

std::string Agent::lastError() const
{
    return m_lastError;
}

int Agent::remainingContext() const
{
    if (!m_model.isLoaded()) return m_model.contextSize();
    int used = m_lastTokenCount.load();
    int rem = m_model.contextSize() - used - 64;
    return rem > 0 ? rem : 0;
}
