#pragma once

#include <string>
#include <map>
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

    void setTemplateKwargs(const std::map<std::string, std::string> &kwargs);
    const std::map<std::string, std::string> &templateKwargs() const;

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
    std::map<std::string, std::string> m_templateKwargs;
};
