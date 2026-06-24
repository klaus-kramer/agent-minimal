// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#include "WebSearchTool.h"
#include "core/JsonHelper.h"

#include <regex>
#include <sstream>
#include <cstdio>
#include <cctype>
#include <memory>
#include <algorithm>

static constexpr size_t MAX_SEARCH_BYTES = 262144;

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp")

static std::string httpGetSearch(const std::string &url, int timeoutSec)
{
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;

    std::wstring wurl(url.begin(), url.end());
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &urlComp)) {
        return {};
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    std::wstring scheme(urlComp.lpszScheme, urlComp.dwSchemeLength);
    bool isSecure = (scheme == L"https");

    HINTERNET hSession = WinHttpOpen(L"agent_minimal/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return {};

    DWORD timeoutMs = static_cast<DWORD>(timeoutSec) * 1000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
        isSecure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return {};
    }

    DWORD flags = isSecure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        NULL, NULL, NULL, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {};
    }

    LPCWSTR acceptTypes[] = { L"*/*", NULL };
    DWORD decompressFlag = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_DECOMPRESSION, &decompressFlag, sizeof(decompressFlag));

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {};
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {};
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        NULL, &statusCode, &statusSize, NULL);
    if (statusCode != 200) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {};
    }

    std::ostringstream os;
    char buffer[8192];
    DWORD bytesRead = 0;
    size_t totalRead = 0;

    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        if (totalRead + bytesRead > MAX_SEARCH_BYTES) {
            os.write(buffer, MAX_SEARCH_BYTES - totalRead);
            break;
        }
        os.write(buffer, bytesRead);
        totalRead += bytesRead;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return os.str();
}

#else

static std::string httpGetSearch(const std::string &url, int timeoutSec)
{
    std::string cmd = "curl -sS --max-time " + std::to_string(timeoutSec)
                    + " --max-filesize " + std::to_string(MAX_SEARCH_BYTES)
                    + " -L \"" + url + "\" 2>/dev/null";

    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};

    std::ostringstream os;
    char buf[4096];
    size_t total = 0;
    while (fgets(buf, sizeof(buf), pipe)) {
        size_t len = strlen(buf);
        if (total + len > MAX_SEARCH_BYTES) {
            os.write(buf, MAX_SEARCH_BYTES - total);
            break;
        }
        os << buf;
        total += len;
    }

    pclose(pipe);
    return os.str();
}

#endif

static std::string urlEncode(const std::string &s)
{
    std::ostringstream encoded;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            encoded << c;
        else
            encoded << '%' << std::hex << std::uppercase << static_cast<int>(c);
    }
    return encoded.str();
}

static std::string htmlDecode(const std::string &s)
{
    std::string r = s;
    static const std::vector<std::pair<std::string, std::string>> entities = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
        {"&quot;", "\""}, {"&#39;", "'"}, {"&#x27;", "'"},
        {"&#x2F;", "/"}, {"&nbsp;", " "}
    };
    for (const auto &e : entities) {
        size_t pos = 0;
        while ((pos = r.find(e.first, pos)) != std::string::npos) {
            r.replace(pos, e.first.size(), e.second);
            pos += e.second.size();
        }
    }
    return r;
}

static std::string searchDuckDuckGo(const std::string &query, int maxResults)
{
    std::string encoded = urlEncode(query);
    std::string url = "https://lite.duckduckgo.com/lite/?q=" + encoded;

    std::string html = httpGetSearch(url, 15);
    if (html.empty()) {
        return "Search failed: empty response";
    }

    std::string result;
    int count = 0;

    std::regex linkRe(R"re(<a\s+rel="nofollow"\s+href="([^"]+)"[^>]*>([^<]*)</a>)re");
    std::regex snippetRe(R"re(<td\s+class="result-snippet">(.*?)</td>)re");

    std::string::const_iterator begin = html.cbegin();
    std::string::const_iterator end = html.cend();
    std::smatch linkMatch, snippetMatch;
    auto snippetPos = html.cbegin();

    while (count < maxResults && std::regex_search(begin, end, linkMatch, linkRe)) {
        std::string href = linkMatch[1].str();
        std::string title = htmlDecode(linkMatch[2].str());

        std::string snippet;
        auto searchStart = linkMatch[0].second;
        std::string::const_iterator snippetBegin = snippetPos;
        if (snippetBegin < searchStart)
            snippetBegin = searchStart;
        if (std::regex_search(snippetBegin, end, snippetMatch, snippetRe)) {
            snippet = htmlDecode(snippetMatch[1].str());
            snippetPos = snippetMatch[0].second;
        }

        snippet = std::regex_replace(snippet, std::regex("<[^>]*>"), "");
        snippet = std::regex_replace(snippet, std::regex("\\s+"), " ");
        if (!snippet.empty() && snippet.back() == ' ')
            snippet.pop_back();

        if (!result.empty())
            result += "\n---\n";
        result += std::to_string(count + 1) + ". " + title + "\n";
        result += "   URL: " + href + "\n";
        if (!snippet.empty())
            result += "   " + snippet;

        begin = linkMatch[0].second;
        ++count;
    }

    if (count == 0) {
        result = "No results found for: " + query;
    } else {
        result = "Search results for \"" + query + "\":\n" + result;
    }

    return result;
}

static ToolResult webSearchTool(const ToolCall &call)
{
    std::string query = json_helper::extractToolArg(call.rawArguments, "query");
    if (query.empty()) return {false, "Missing parameter: query"};

    int maxResults = 5;
    std::string maxStr = json_helper::extractToolArg(call.rawArguments, "max_results");
    if (!maxStr.empty()) {
        try {
            maxResults = std::stoi(maxStr);
            if (maxResults < 1) maxResults = 1;
            if (maxResults > 20) maxResults = 20;
        } catch (...) {}
    }

    std::string results = searchDuckDuckGo(query, maxResults);
    return {true, results};
}

void registerWebSearchTool(ToolRegistry &registry)
{
    ToolDefinition def;
    def.name = "web_search";
    def.description = "Search the web using DuckDuckGo. Returns a list of results with titles, URLs, and snippets.";
    def.parameters = {
        {"query", "The search query", "string", true},
        {"max_results", "Maximum number of results (1-20, default: 5)", "integer", false}
    };
    def.requiresConfirmation = false;
    registry.registerTool(def, webSearchTool);
}
