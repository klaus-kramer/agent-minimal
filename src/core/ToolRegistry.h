#pragma once

#include "Tool.h"
#include <string>
#include <vector>
#include <ostream>

class ToolRegistry {
public:
    void registerTool(const ToolDefinition& def, ToolExecuteFn fn);

    void setContext(const std::string& context) { m_context = context; }
    const std::string& context() const { return m_context; }

    std::string generateToolPrompt() const;
    ToolCall parseToolCall(const std::string& response);

    const ToolDefinition* findTool(const std::string& name) const;
    ToolResult executeTool(const ToolCall& call) const;

    void listTools(std::ostream &os) const;
    bool isEmpty() const;
    void clear();

private:
    struct RegisteredTool {
        ToolDefinition def;
        ToolExecuteFn executeFn;
    };

    std::vector<RegisteredTool> m_tools;
    std::string m_context;
};
