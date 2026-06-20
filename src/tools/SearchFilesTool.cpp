// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#include "SearchFilesTool.h"
#include "core/JsonHelper.h"

#include <regex>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

static constexpr int MAX_FILES_DEFAULT = 50;
static constexpr int MAX_FILES_LIMIT = 500;

static std::string globToRegex(const std::string &glob)
{
    std::string re;
    re.reserve(glob.size() + 4);
    for (size_t i = 0; i < glob.size(); ++i) {
        char c = glob[i];
        switch (c) {
            case '*': re += ".*";  break;
            case '?': re += ".";   break;
            case '.': re += "\\."; break;
            case '+': re += "\\+"; break;
            case '(': re += "\\("; break;
            case ')': re += "\\)"; break;
            case '[': re += "[";   break;
            case ']': re += "]";   break;
            case '{': re += "\\{"; break;
            case '}': re += "\\}"; break;
            case '^': re += "\\^"; break;
            case '$': re += "\\$"; break;
            case '|': re += "\\|"; break;
            case '\\': re += "\\\\"; break;
            default:  re += c;     break;
        }
    }
    return re;
}

static ToolResult searchFilesTool(const ToolCall &call)
{
    std::string glob = json_helper::extractToolArg(call.rawArguments, "pattern");
    if (glob.empty())
        glob = "*";

    if (glob.find_first_of("*?") == std::string::npos && glob.front() != '*')
        glob = "*" + glob;

    std::string searchPath = json_helper::extractToolArg(call.rawArguments, "path");
    if (searchPath.empty())
        searchPath = ".";

    std::string maxStr = json_helper::extractToolArg(call.rawArguments, "max_results");
    int maxResults = MAX_FILES_DEFAULT;
    if (!maxStr.empty()) {
        try {
            maxResults = std::stoi(maxStr);
            if (maxResults < 1) maxResults = 1;
            if (maxResults > MAX_FILES_LIMIT) maxResults = MAX_FILES_LIMIT;
        } catch (...) {}
    }

    fs::path root = fs::absolute(searchPath).lexically_normal();
    if (!fs::exists(root))
        return {false, "Path not found: " + searchPath};
    if (!fs::is_directory(root))
        return {false, "Not a directory: " + searchPath};

    std::regex re;
    try {
        re = std::regex(globToRegex(glob), std::regex::icase);
    } catch (const std::regex_error &e) {
        return {false, "Invalid glob pattern: " + std::string(e.what())};
    }

    static const char *skipDirNames[] = {
        "build", "_deps", ".git", ".svn", ".hg", ".cache",
        "__pycache__", "node_modules", ".idea", ".vscode"
    };

    std::ostringstream os;
    int totalFiles = 0;
    bool truncated = false;

    os << "Files matching \"" << glob << "\" in " << root.string() << ":\n\n";

    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied); it != fs::recursive_directory_iterator(); ++it) {
        const auto &entry = *it;

        if (entry.is_directory()) {
            bool skipDir = false;
            std::string dirName = entry.path().filename().string();
#ifdef _WIN32
            for (auto &c : dirName) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
            for (auto *dn : skipDirNames) {
                if (dirName == dn) { skipDir = true; break; }
            }
            if (skipDir) {
                it.disable_recursion_pending();
                continue;
            }
        }

        if (!entry.is_regular_file())
            continue;

        std::string filename = entry.path().filename().string();
        if (!std::regex_match(filename, re))
            continue;

        std::string relativePath = fs::relative(entry.path(), root).string();
        os << "  " << relativePath << "\n";
        ++totalFiles;

        if (totalFiles >= maxResults) {
            truncated = true;
            break;
        }
    }

    if (totalFiles == 0) {
        os << "  (no matches found)\n";
    }

    if (truncated)
        os << "\n(" << totalFiles << " files shown, more may exist. Increase max_results up to " << MAX_FILES_LIMIT << ")\n";
    else
        os << "\n(" << totalFiles << " files found)\n";

    return {true, os.str()};
}

void registerSearchFilesTool(ToolRegistry &registry)
{
    ToolDefinition def;
    def.name = "search_files";
    def.description = "Find files by glob pattern (e.g. \"*.h\", \"*.{cpp,h}\"). Recursively searches within the project directory.";
    def.parameters = {
        {"pattern", "Glob pattern to match filenames (e.g. \"*.cpp\", \"*.{ts,tsx}\"). Default: *", "string", false},
        {"path", "Optional directory to search in. Defaults to current directory", "string", false},
        {"max_results", "Maximum number of results (default: 50, max: 500)", "integer", false}
    };
    def.requiresConfirmation = false;
    registry.registerTool(def, searchFilesTool);
}
