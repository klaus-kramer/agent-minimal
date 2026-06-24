// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#include "FileTool.h"
#include "core/JsonHelper.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cctype>
#include <algorithm>

namespace fs = std::filesystem;

static constexpr size_t MAX_FILE_TOOL_BYTES = 102400;
static constexpr size_t MAX_WRITE_TOOL_BYTES = 102400;

static fs::path &getAllowedRoot()
{
    static fs::path root = fs::current_path();
    return root;
}

void setAllowedRoot(const std::string &path)
{
    getAllowedRoot() = fs::absolute(path);
}

static fs::path resolveAndCheck(const fs::path& p)
{
#ifdef _WIN32

    std::string pStr = p.string();
    if (pStr.size() == 2 && std::isalpha(static_cast<unsigned char>(pStr[0])) && pStr[1] == ':') {
        pStr += '\\';
        return resolveAndCheck(fs::path(pStr));
    }
#endif

    fs::path resolved = fs::absolute(p).lexically_normal();

    auto root = getAllowedRoot().lexically_normal();
    std::string rstr = resolved.string();
    std::string rootStr = root.string();

#ifdef _WIN32
    for (auto &c : rstr) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (auto &c : rootStr) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif

    if (rstr.size() < rootStr.size() || rstr.compare(0, rootStr.size(), rootStr) != 0) {
        throw std::runtime_error("Access denied: path '" + p.string() + "' is outside the project directory ('" + root.string() + "')");
    }

    if (rstr.size() > rootStr.size() && rstr[rootStr.size()] != '/' && rstr[rootStr.size()] != '\\') {
        throw std::runtime_error("Access denied: path '" + p.string() + "' is outside the project directory ('" + root.string() + "')");
    }

    return resolved;
}

static ToolResult readFileTool(const ToolCall& call)
{
    std::string path = json_helper::extractToolArg(call.rawArguments, "path");
    if (path.empty()) return {false, "Missing parameter: path"};

    fs::path fp;
    try {
        fp = resolveAndCheck(path);
    } catch (const std::exception& e) {
        return {false, e.what()};
    }

    if (!fs::exists(fp)) return {false, "File not found: " + path};
    if (!fs::is_regular_file(fp)) return {false, "Not a regular file: " + path};

    std::ifstream f(fp, std::ios::binary | std::ios::ate);
    if (!f) return {false, "Could not open file: " + path};

    std::streamsize size = f.tellg();
    f.seekg(0);

    bool truncated = false;
    size_t readSize = static_cast<size_t>(size);
    if (readSize > MAX_FILE_TOOL_BYTES) {
        readSize = MAX_FILE_TOOL_BYTES;
        truncated = true;
    }

    std::string content(readSize, '\0');
    if (readSize > 0) f.read(content.data(), readSize);

    std::ostringstream os;
    os << "File: " << fp.filename().string() << "\n";
    os << "Path: " << fp.string() << "\n";
    os << "Size: " << size << " bytes\n";
    os << "--- BEGIN FILE ---\n";
    os << content;
    os << "\n--- END FILE ---\n";
    if (truncated)
        os << "(File truncated at " << MAX_FILE_TOOL_BYTES
           << " bytes, " << (size - static_cast<std::streamoff>(MAX_FILE_TOOL_BYTES))
           << " bytes omitted)\n";

    return {true, os.str()};
}

static ToolResult listDirTool(const ToolCall& call)
{
    std::string path = json_helper::extractToolArg(call.rawArguments, "path");
    if (path.empty()) path = ".";

    fs::path dp;
    try {
        dp = resolveAndCheck(path);
    } catch (const std::exception& e) {
        return {false, e.what()};
    }

    if (!fs::exists(dp)) return {false, "Directory not found: " + path};
    if (!fs::is_directory(dp)) return {false, "Not a directory: " + path};

    std::ostringstream os;
    os << "Contents of: " << dp.string() << "\n\n";

    int fileCount = 0, dirCount = 0;
    for (const auto& entry : fs::directory_iterator(dp)) {
        auto name = entry.path().filename().string();
        if (entry.is_directory()) {
            os << "[DIR]  " << name << "\n";
            ++dirCount;
        } else if (entry.is_regular_file()) {
            auto sz = entry.file_size();
            os << "[FILE] " << name << " (" << sz << " Bytes)\n";
            ++fileCount;
        } else {
            os << "[OTHER] " << name << "\n";
        }
    }

    os << "\n" << dirCount << " directories, " << fileCount << " files\n";
    return {true, os.str()};
}

static ToolResult writeFileTool(const ToolCall& call)
{
    std::string path = json_helper::extractToolArg(call.rawArguments, "path");
    std::string content = json_helper::extractToolArg(call.rawArguments, "content");
    if (path.empty()) return {false, "Missing parameter: path"};
    if (content.empty()) return {false, "Missing parameter: content"};

    fs::path fp;
    try {
        fp = resolveAndCheck(path);
    } catch (const std::exception& e) {
        return {false, e.what()};
    }

    auto parent = fp.parent_path();
    if (!parent.empty() && !fs::exists(parent)) {
        std::error_code ec;
        if (!fs::create_directories(parent, ec))
            return {false, "Failed to create parent directories: " + ec.message()};
    }

    std::ofstream f(fp, std::ios::binary);
    if (!f) return {false, "Could not open file for writing: " + path};

    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!f) return {false, "Failed to write to file: " + path};

    size_t written = content.size();
    std::ostringstream os;
    os << "File written: " << fp.string() << "\n";
    os << "Size: " << written << " bytes\n";
    return {true, os.str()};
}

static ToolResult editFileTool(const ToolCall& call)
{
    std::string path      = json_helper::extractToolArg(call.rawArguments, "path");
    std::string oldString = json_helper::extractToolArg(call.rawArguments, "old_string");
    std::string newString = json_helper::extractToolArg(call.rawArguments, "new_string");
    std::string allStr    = json_helper::extractToolArg(call.rawArguments, "replace_all");
    bool replaceAll = (allStr == "true" || allStr == "1");

    if (path.empty())      return {false, "Missing parameter: path"};
    if (oldString.empty()) return {false, "Missing parameter: old_string"};

    fs::path fp;
    try {
        fp = resolveAndCheck(path);
    } catch (const std::exception& e) {
        return {false, e.what()};
    }

    if (!fs::exists(fp))        return {false, "File not found: " + path};
    if (!fs::is_regular_file(fp)) return {false, "Not a regular file: " + path};

    auto size = fs::file_size(fp);
    if (size > MAX_FILE_TOOL_BYTES)
        return {false, "File too large (" + std::to_string(size) + " bytes). Max " + std::to_string(MAX_FILE_TOOL_BYTES) + " bytes."};

    std::ifstream f(fp, std::ios::binary);
    if (!f) return {false, "Could not open file: " + path};

    std::string content(static_cast<size_t>(size), '\0');
    f.read(content.data(), static_cast<std::streamsize>(size));
    f.close();

    size_t pos = content.find(oldString);
    if (pos == std::string::npos)
        return {false, "old_string not found in file: " + path};

    int lineNum = 1;
    for (size_t i = 0; i < pos; ++i) {
        if (content[i] == '\n') ++lineNum;
    }

    int count = 0;
    if (replaceAll) {
        size_t start = 0;
        while ((start = content.find(oldString, start)) != std::string::npos) {
            content.replace(start, oldString.size(), newString);
            start += newString.size();
            ++count;
        }
    } else {
        content.replace(pos, oldString.size(), newString);
        count = 1;
    }

    std::ofstream of(fp, std::ios::binary);
    if (!of) return {false, "Could not open file for writing: " + path};
    of.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!of) return {false, "Failed to write to file: " + path};

    std::ostringstream os;
    os << "File edited: " << fp.string() << "\n";
    os << "Line: " << lineNum << " | " << count << " replacement(s) made\n";
    return {true, os.str()};
}

void registerFileTools(ToolRegistry& registry)
{
    ToolDefinition readDef;
    readDef.name = "read_file";
    readDef.description = "Read the content of a file. Maximum 100 KB. Restricted to the project directory.";
    readDef.parameters = {
        {"path", "The path to the file (relative or absolute, within project)", "string", true}
    };
    readDef.requiresConfirmation = false;
    registry.registerTool(readDef, readFileTool);

    ToolDefinition listDef;
    listDef.name = "list_directory";
    listDef.description = "List files and subdirectories of a path. Restricted to the project directory.";
    listDef.parameters = {
        {"path", "The path to the directory (default: current directory)", "string", false}
    };
    listDef.requiresConfirmation = false;
    registry.registerTool(listDef, listDirTool);

    ToolDefinition writeDef;
    writeDef.name = "write_file";
    writeDef.description = "Write content to a file. Creates parent directories if needed. Maximum 100 KB. Restricted to the project directory.";
    writeDef.parameters = {
        {"path", "The path to the file (relative or absolute, within project)", "string", true},
        {"content", "The content to write to the file", "string", true}
    };
    writeDef.requiresConfirmation = true;
    registry.registerTool(writeDef, writeFileTool);

    ToolDefinition editDef;
    editDef.name = "edit_file";
    editDef.description = "Replace text in an existing file. Specify the exact old_string to find and the new_string to replace it with. Use replace_all to replace every occurrence. Maximum 100 KB. Restricted to the project directory.";
    editDef.parameters = {
        {"path", "The path to the file (relative or absolute, within project)", "string", true},
        {"old_string", "The exact text to search for", "string", true},
        {"new_string", "The replacement text", "string", true},
        {"replace_all", "If true, replace all occurrences instead of just the first (default: false)", "string", false}
    };
    editDef.requiresConfirmation = true;
    registry.registerTool(editDef, editFileTool);
}
