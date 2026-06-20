// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#include "SearchPatternTool.h"
#include "core/JsonHelper.h"

#include <regex>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

static constexpr size_t MAX_SEARCH_FILE_SIZE = 524288;
static constexpr int MAX_RESULTS_PER_FILE = 100;
static constexpr int MAX_TOTAL_MATCHES = 30;
static constexpr size_t MAX_LINE_LENGTH = 4096;

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

static bool matchesFilter(const fs::path &filePath, const std::string &includePattern)
{
    if (includePattern.empty())
        return true;

    std::string filename = filePath.filename().string();
    std::regex filterRe(globToRegex(includePattern), std::regex::icase);
    return std::regex_match(filename, filterRe);
}

static ToolResult searchPatternTool(const ToolCall &call)
{
    std::string pattern = json_helper::extractToolArg(call.rawArguments, "pattern");
    if (pattern.empty())
        return {false, "Missing required parameter: pattern (regex to search for)"};

    std::string includeFilter = json_helper::extractToolArg(call.rawArguments, "include");
    std::string searchPath    = json_helper::extractToolArg(call.rawArguments, "path");

    if (searchPath.empty())
        searchPath = ".";

    fs::path root = fs::absolute(searchPath).lexically_normal();
    if (!fs::exists(root))
        return {false, "Path not found: " + searchPath};
    if (!fs::is_directory(root))
        return {false, "Not a directory: " + searchPath};

    std::regex re;
    try {
        re = std::regex(pattern, std::regex::ECMAScript | std::regex::icase);
    } catch (const std::regex_error &e) {
        return {false, "Invalid regex pattern: " + std::string(e.what())};
    }

    std::ostringstream os;
    int totalMatches = 0;
    int fileCount = 0;
    bool truncated = false;

    static const char *skipDirNames[] = {
        "build", "_deps", ".git", ".svn", ".hg", ".cache",
        "__pycache__", "node_modules", ".idea", ".vscode"
    };

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

        if (fs::file_size(entry.path()) > MAX_SEARCH_FILE_SIZE)
            continue;
        if (!matchesFilter(entry.path(), includeFilter))
            continue;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        static const char *binaryExts[] = {
            ".exe", ".dll", ".obj", ".lib", ".pdb", ".png", ".jpg",
            ".jpeg", ".gif", ".bmp", ".ico", ".zip", ".7z", ".gz",
            ".ttf", ".otf", ".woff", ".woff2", ".o", ".a", ".so",
            ".dylib", ".pyc", ".pyo", ".class", ".jar", ".dex",
            ".recipe", ".tlog", ".lastbuildstate"
        };
        bool isBinary = false;
        for (auto *be : binaryExts) {
            if (ext == be) { isBinary = true; break; }
        }
        if (isBinary) continue;

        std::ifstream f(entry.path());
        if (!f) continue;

        std::string relativePath = fs::relative(entry.path(), root).string();
        int fileMatches = 0;
        std::string line;
        int lineNum = 0;

        while (std::getline(f, line)) {
            ++lineNum;
            if (line.size() > MAX_LINE_LENGTH)
                line.resize(MAX_LINE_LENGTH);

            std::sregex_iterator it(line.begin(), line.end(), re);
            std::sregex_iterator end;
            if (it != end) {
                if (fileMatches == 0) {
                    if (totalMatches > 0) os << "\n";
                    os << relativePath << "\n";
                }

                int matchGroup = 0;
                for (auto m = it; m != end && matchGroup < MAX_RESULTS_PER_FILE; ++m, ++matchGroup) {
                    os << "  " << lineNum << ": " << line << "\n";
                    ++totalMatches;
                    ++fileMatches;
                    if (totalMatches >= MAX_TOTAL_MATCHES) {
                        truncated = true;
                        break;
                    }
                }
                if (totalMatches >= MAX_TOTAL_MATCHES) break;
            }
            if (fileMatches >= MAX_RESULTS_PER_FILE) {
                os << "  (... more matches in " << relativePath << ")\n";
                break;
            }
        }

        if (fileMatches > 0) ++fileCount;
        if (truncated) break;
    }

    if (totalMatches == 0) {
        std::string msg = "No matches found for pattern: " + pattern;
        if (!includeFilter.empty())
            msg += " (filter: " + includeFilter + ")";
        return {true, msg};
    }

    std::string result = os.str();
    if (truncated)
        result += "\n(Results truncated at " + std::to_string(MAX_TOTAL_MATCHES) + " matches)";

    std::string summary = "Found " + std::to_string(totalMatches)
        + " matches in " + std::to_string(fileCount) + " files:\n\n";
    summary += result;
    if (summary.size() > 6000) {
        summary.resize(6000);
        summary += "\n...(output truncated at 6000 chars)";
    }
    return {true, summary};
}

void registerSearchPatternTool(ToolRegistry &registry)
{
    ToolDefinition def;
    def.name = "search_pattern";
    def.description = "Search file contents using a regex pattern. Supports filtering by file pattern (e.g. \"*.cpp\", \"*.{h,ts}\"). Scans recursively within the project directory.";
    def.parameters = {
        {"pattern", "The regex pattern to search for (case-insensitive)", "string", true},
        {"include", "Optional file glob filter (e.g. \"*.cpp\", \"*.{ts,tsx}\"). Default: all files", "string", false},
        {"path", "Optional directory to search in. Defaults to current directory", "string", false}
    };
    def.requiresConfirmation = false;
    registry.registerTool(def, searchPatternTool);
}
