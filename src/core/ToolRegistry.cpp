// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#include "ToolRegistry.h"
#include "JsonHelper.h"
#include <sstream>

void ToolRegistry::registerTool(const ToolDefinition& def, ToolExecuteFn fn)
{
    m_tools.push_back({def, std::move(fn)});
}

std::string ToolRegistry::generateToolPrompt() const
{
    if (m_tools.empty()) return {};

    std::ostringstream os;
    os << "\nYou have access to tools. "
          "To use a tool, respond with JSON: {\"function\": \"tool_name\", \"arguments\": {params}}.\n"
          "For normal conversation, just reply naturally.\n\n"
          "Available tools:\n";

    for (const auto& rt : m_tools) {
        os << "- " << rt.def.name << "(";
        bool first = true;
        for (const auto& p : rt.def.parameters) {
            if (!first) os << ", ";
            os << p.name << ": " << p.type;
            if (!p.required) os << "?";
            first = false;
        }
        os << ")";
        if (!rt.def.description.empty())
            os << ": " << rt.def.description;
        os << "\n";
    }

    return os.str();
}

ToolCall ToolRegistry::parseToolCall(const std::string& response)
{

    std::string name = json_helper::extractStringValue(response, "function");
    if (!name.empty()) {
        std::string args = json_helper::extractObjectValue(response, "arguments");
        if (args.empty()) return {name, "{}"};

        if (args[0] == '"') {

            size_t end = json_helper::findStringEnd(args, 0);
            if (end != std::string::npos && end >= 2) {
                std::string inner = args.substr(1, end - 2);
                inner = json_helper::unescapeJsonString(inner);
                if (!inner.empty() && (inner[0] == '{' || inner[0] == '['))
                    args = inner;
            }
        }
        return {name, args};
    }

    std::string rawObj;
    name = json_helper::extractGemmaCall(response, rawObj);
    if (!name.empty()) {

        return {name, rawObj};
    }

    for (const auto &rt : m_tools) {
        std::string pattern = rt.def.name + "(";
        size_t pos = response.find(pattern);
        if (pos == std::string::npos) continue;
        size_t parenPos = pos + rt.def.name.size();
        size_t objEnd = json_helper::findValueEnd(response, parenPos);
        if (objEnd == std::string::npos) continue;
        std::string rawArgs = response.substr(parenPos, objEnd - parenPos);
        if (rawArgs.size() >= 2 && rawArgs[0] == '(' && rawArgs.back() == ')') {
            std::string inner = rawArgs.substr(1, rawArgs.size() - 2);
            for (char &c : inner) if (c == '=') c = ':';
            rawObj = "{" + inner + "}";
        }
        return {rt.def.name, rawObj};
    }

    return {"", ""};
}

const ToolDefinition* ToolRegistry::findTool(const std::string& name) const
{
    for (const auto& rt : m_tools) {
        if (rt.def.name == name) return &rt.def;
    }
    return nullptr;
}

ToolResult ToolRegistry::executeTool(const ToolCall& call) const
{
    for (const auto& rt : m_tools) {
        if (rt.def.name == call.name) {
            return rt.executeFn(call);
        }
    }
    return {false, "Unknown tool: " + call.name};
}

void ToolRegistry::listTools(std::ostream &os) const
{
    if (m_tools.empty()) {
        os << "  (no tools registered)\n";
        return;
    }

    for (const auto &rt : m_tools) {
        os << "  - " << rt.def.name;
        if (!rt.def.description.empty())
            os << ": " << rt.def.description;
        if (rt.def.requiresConfirmation)
            os << " (requires confirmation)";
        os << "\n";
        for (const auto &p : rt.def.parameters) {
            os << "      " << p.name << " (" << p.type << ")";
            if (!p.description.empty()) os << ": " << p.description;
            if (!p.required) os << " (optional)";
            os << "\n";
        }
    }
}

bool ToolRegistry::isEmpty() const
{
    return m_tools.empty();
}

void ToolRegistry::clear()
{
    m_tools.clear();
}
