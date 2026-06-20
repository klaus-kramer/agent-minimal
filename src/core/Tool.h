// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#pragma once

#include <string>
#include <vector>
#include <functional>

struct ToolParameter {
    std::string name;
    std::string description;
    std::string type;
    bool required = true;
};

struct ToolDefinition {
    std::string name;
    std::string description;
    std::vector<ToolParameter> parameters;
    bool requiresConfirmation = false;
};

struct ToolCall {
    std::string name;
    std::string rawArguments;
};

struct ToolResult {
    bool success;
    std::string output;
};

using ToolExecuteFn = std::function<ToolResult(const ToolCall&)>;
