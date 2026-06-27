// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#include "ChatHistory.h"
#include "Model.h"

#include <algorithm>
#include <cctype>
#include <cstring>

void ChatHistory::setSystemPrompt(const std::string &prompt)
{
    m_systemPrompt = prompt;
}

std::string ChatHistory::systemPrompt() const
{
    return m_systemPrompt;
}

void ChatHistory::addMessage(const std::string &role, const std::string &content)
{
    m_messages.push_back({role, content});
}

void ChatHistory::clear()
{
    m_messages.clear();
}

static bool isGemma4(const Model &model)
{
    char desc[256] = {};
    if (llama_model_desc(model.model(), desc, sizeof(desc)) <= 0)
        return false;
    std::string name = desc;
    for (auto &c : name)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return name.find("gemma4")  != std::string::npos ||
           name.find("gemma-4") != std::string::npos ||
           name.find("gemma 4") != std::string::npos;
}

static std::string buildGemma4Prompt(
    const std::string &systemPrompt,
    const std::vector<ChatHistory::Message> &messages,
    bool addAssistant)
{
    std::string result;

    bool hasContent = false;
    auto append = [&](const std::string &s) { result += s; hasContent = true; };

    if (!systemPrompt.empty()) {
        append("<|turn>system\n");
        append(systemPrompt);
        append("<turn|>\n");
    }

    for (const auto &msg : messages) {
        std::string role = msg.role;
        if (role == "assistant") role = "model";
        append("<|turn>" + role + "\n");
        append(msg.content);
        append("<turn|>\n");
    }

    if (addAssistant) {
        append("<|turn>model\n");
    }

    return result;
}

static std::string detectBuiltinTemplate(const Model &model)
{

    char desc[256] = {};
    if (llama_model_desc(model.model(), desc, sizeof(desc)) <= 0)
        return {};

    std::string name = desc;
    for (auto &c : name)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (name.find("gemma4")  != std::string::npos ||
        name.find("gemma-4") != std::string::npos ||
        name.find("gemma 4") != std::string::npos) return "gemma4";
    if (name.find("gemma")   != std::string::npos) return "gemma";
    if (name.find("llama4")  != std::string::npos) return "llama4";
    if (name.find("llama3")  != std::string::npos) return "llama3";
    if (name.find("llama2")  != std::string::npos) return "llama2";
    if (name.find("phi4")    != std::string::npos) return "phi4";
    if (name.find("phi3")    != std::string::npos) return "phi3";
    if (name.find("phi")     != std::string::npos) return "phi3";
    if (name.find("deepseek")!= std::string::npos) return "deepseek";
    if (name.find("qwen")    != std::string::npos) return "chatml";
    if (name.find("mistral") != std::string::npos) return "mistral-v7";
    if (name.find("chatml")  != std::string::npos) return "chatml";
    if (name.find("command") != std::string::npos) return "command-r";
    if (name.find("vicuna")  != std::string::npos) return "vicuna";
    if (name.find("zephyr")  != std::string::npos) return "zephyr";

    const char *t = llama_model_chat_template(model.model(), nullptr);
    return t ? std::string(t) : std::string();
}

std::string ChatHistory::applyTemplate(const Model &model, bool addAssistant) const
{
    if (isGemma4(model)) {
        return buildGemma4Prompt(m_systemPrompt, m_messages, addAssistant);
    }

    std::string tmpl = detectBuiltinTemplate(model);
    if (tmpl.empty())
        return {};

    std::vector<Message> all;
    if (!m_systemPrompt.empty())
        all.push_back({"system", m_systemPrompt});
    all.insert(all.end(), m_messages.begin(), m_messages.end());

    std::vector<llama_chat_message> chat;
    chat.reserve(all.size());
    for (const auto &msg : all)
        chat.push_back({msg.role.c_str(), msg.content.c_str()});

    int32_t len = llama_chat_apply_template(
        tmpl.c_str(), chat.data(), chat.size(), addAssistant, nullptr, 0);

    if (len <= 0)
        return {};

    std::string result(static_cast<size_t>(len) - 1, '\0');
    llama_chat_apply_template(
        tmpl.c_str(), chat.data(), chat.size(), addAssistant, result.data(), len);

    return result;
}

size_t ChatHistory::messageCount() const
{
    return m_messages.size();
}

void ChatHistory::trimToFit(const Model &model, int maxPromptTokens)
{
    while (!m_messages.empty()) {
        std::string prompt = applyTemplate(model, true);
        if (prompt.empty()) break;

        const auto *vocab = model.vocab();
        int n = llama_tokenize(vocab,
            prompt.c_str(), static_cast<int>(prompt.size()),
            nullptr, 0, true, true);

        if (n >= 0 && n <= maxPromptTokens)
            break;

        if (m_messages.size() <= 1) {
            m_messages.clear();
            break;
        }

        m_messages.erase(m_messages.begin() + 1);
    }
}
