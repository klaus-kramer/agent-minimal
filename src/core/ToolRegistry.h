// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#pragma once

#include "Tool.h"
#include <string>
#include <vector>
#include <ostream>

class ToolRegistry {
public:
    void registerTool(const ToolDefinition& def, ToolExecuteFn fn);

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
};
