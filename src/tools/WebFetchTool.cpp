// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#include "WebFetchTool.h"
#include "core/JsonHelper.h"

#include <sstream>
#include <cstdio>
#include <memory>
#include <stdexcept>

static constexpr size_t MAX_WEB_BYTES = 524288;

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp")

static ToolResult fetchViaWinHTTP(const std::string &url, int timeoutSec)
{
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;

    std::wstring wurl(url.begin(), url.end());
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &urlComp)) {
        return {false, "Failed to parse URL"};
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    std::wstring scheme(urlComp.lpszScheme, urlComp.dwSchemeLength);
    bool isSecure = (scheme == L"https");

    HINTERNET hSession = WinHttpOpen(L"agent_minimal/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) {
        return {false, "WinHttpOpen failed"};
    }

    DWORD timeoutMs = static_cast<DWORD>(timeoutSec) * 1000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
        isSecure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return {false, "WinHttpConnect failed"};
    }

    DWORD flags = isSecure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        NULL, NULL, NULL, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {false, "WinHttpOpenRequest failed"};
    }

    LPCWSTR acceptTypes[] = { L"*/*", NULL };
    DWORD decompressFlag = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_DECOMPRESSION, &decompressFlag, sizeof(decompressFlag));

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {false, "WinHttpSendRequest failed"};
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {false, "WinHttpReceiveResponse failed"};
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        NULL, &statusCode, &statusSize, NULL);
    if (statusCode != 200) {
        std::string err = "HTTP " + std::to_string(statusCode);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {false, err};
    }

    std::ostringstream os;
    char buffer[8192];
    DWORD bytesRead = 0;
    size_t totalRead = 0;
    bool truncated = false;

    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        if (totalRead + bytesRead > MAX_WEB_BYTES) {
            os.write(buffer, MAX_WEB_BYTES - totalRead);
            truncated = true;
            break;
        }
        os.write(buffer, bytesRead);
        totalRead += bytesRead;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    std::string content = os.str();
    if (truncated) {
        content += "\n(Response truncated at " + std::to_string(MAX_WEB_BYTES) + " bytes)";
    }

    return {true, content};
}

#else

static ToolResult fetchViaCurlCLI(const std::string &url, int timeoutSec)
{
    std::string cmd = "curl -sS --max-time " + std::to_string(timeoutSec)
                    + " --max-filesize " + std::to_string(MAX_WEB_BYTES)
                    + " -L \"" + url + "\" 2>/dev/null";

    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return {false, "Failed to execute curl"};
    }

    std::ostringstream os;
    char buf[4096];
    size_t total = 0;
    while (fgets(buf, sizeof(buf), pipe)) {
        size_t len = strlen(buf);
        if (total + len > MAX_WEB_BYTES) {
            os.write(buf, MAX_WEB_BYTES - total);
            os << "\n(Response truncated at " << MAX_WEB_BYTES << " bytes)";
            break;
        }
        os << buf;
        total += len;
    }

    int status = pclose(pipe);
    if (status != 0 && os.tellp() == 0) {
        return {false, "curl exited with status " + std::to_string(status)};
    }

    return {true, os.str()};
}

#endif

static ToolResult webFetchTool(const ToolCall &call)
{
    std::string url = json_helper::extractToolArg(call.rawArguments, "url");
    if (url.empty()) return {false, "Missing parameter: url"};

    int timeoutSec = 30;
    std::string timeoutStr = json_helper::extractToolArg(call.rawArguments, "timeout");
    if (!timeoutStr.empty()) {
        try { timeoutSec = std::stoi(timeoutStr); } catch (...) {}
    }

    if (url.empty()) {
        return {false, "Empty URL"};
    }

    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
        return {false, "Only http:// and https:// URLs are supported"};
    }

#ifdef _WIN32
    return fetchViaWinHTTP(url, timeoutSec);
#else
    return fetchViaCurlCLI(url, timeoutSec);
#endif
}

void registerWebFetchTool(ToolRegistry &registry)
{
    ToolDefinition def;
    def.name = "web_fetch";
    def.description = "Fetch content from a URL. Returns the raw response body.";
    def.parameters = {
        {"url", "The URL to fetch", "string", true},
        {"timeout", "Timeout in seconds (default: 30)", "integer", false}
    };
    def.requiresConfirmation = true;
    registry.registerTool(def, webFetchTool);
}
