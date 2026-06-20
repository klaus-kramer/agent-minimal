// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#pragma once

#include <string>
#include <vector>

#include "llama.h"

class Model;

class ChatHistory
{
public:
    void setSystemPrompt(const std::string &prompt);
    std::string systemPrompt() const;

    void addMessage(const std::string &role, const std::string &content);
    void clear();

    std::string applyTemplate(const Model &model, bool addAssistant = true) const;

    size_t messageCount() const;

    void trimToFit(const Model &model, int maxPromptTokens);

    struct Message
    {
        std::string role;
        std::string content;
    };

private:

    std::vector<Message> m_messages;
    std::string m_systemPrompt;
};
