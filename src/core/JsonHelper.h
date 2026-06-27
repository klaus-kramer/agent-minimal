// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#pragma once

#include <string>
#include <vector>
#include <cctype>

namespace json_helper {

inline size_t skipWhitespace(const std::string &s, size_t pos)
{
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
        ++pos;
    return pos;
}

inline size_t findStringEnd(const std::string &s, size_t pos)
{
    if (pos >= s.size() || s[pos] != '"') return pos;
    ++pos;
    while (pos < s.size()) {
        if (s[pos] == '\\') { pos += 2; continue; }
        if (s[pos] == '"') return pos + 1;
        ++pos;
    }
    return std::string::npos;
}

inline std::string unescapeJsonString(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
                case '"':  out += '"'; ++i; break;
                case '\\': out += '\\'; ++i; break;
                case '/':  out += '/'; ++i; break;
                case 'n':  out += '\n'; ++i; break;
                case 't':  out += '\t'; ++i; break;
                default:   out += s[i]; break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

inline size_t findValueEnd(const std::string &s, size_t pos)
{
    if (pos >= s.size()) return std::string::npos;
    pos = skipWhitespace(s, pos);
    if (pos >= s.size()) return std::string::npos;

    char c = s[pos];
    if (c == '"') return findStringEnd(s, pos);
    if (c == '{' || c == '[' || c == '(') {
        char open = c;
        char close = (c == '{') ? '}' : (c == '[') ? ']' : ')';
        int depth = 0;
        bool inStr = false;
        for (size_t i = pos; i < s.size(); ++i) {
            if (inStr) {
                if (s[i] == '\\') { ++i; continue; }
                if (s[i] == '"') inStr = false;
                continue;
            }
            if (s[i] == '"') { inStr = true; continue; }
            if (s[i] == open) ++depth;
            if (s[i] == close) { --depth; if (depth == 0) return i + 1; }
        }
        return std::string::npos;
    }

    size_t i = pos;
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && !(s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
        ++i;
    return i;
}

inline std::string extractStringValue(const std::string &s, const std::string &key)
{
    std::string searchKey = "\"" + key + "\"";
    size_t pos = 0;
    while (true) {
        pos = s.find(searchKey, pos);
        if (pos == std::string::npos) return {};

        size_t before = skipWhitespace(s, pos + searchKey.size());
        if (before >= s.size() || s[before] != ':') {
            pos += searchKey.size();
            continue;
        }
        size_t valStart = skipWhitespace(s, before + 1);
        if (valStart >= s.size() || s[valStart] != '"') {
            pos += searchKey.size();
            continue;
        }
        size_t valEnd = findStringEnd(s, valStart);
        if (valEnd == std::string::npos) return {};
        return s.substr(valStart + 1, valEnd - valStart - 2);
    }
}

inline std::string extractObjectValue(const std::string &s, const std::string &key)
{
    std::string searchKey = "\"" + key + "\"";
    size_t pos = 0;
    while (true) {
        pos = s.find(searchKey, pos);
        if (pos == std::string::npos) return {};

        size_t before = skipWhitespace(s, pos + searchKey.size());
        if (before >= s.size() || s[before] != ':') {
            pos += searchKey.size();
            continue;
        }
        size_t valStart = skipWhitespace(s, before + 1);
        size_t valEnd = findValueEnd(s, valStart);
        if (valEnd == std::string::npos) return {};
        return s.substr(valStart, valEnd - valStart);
    }
}

inline std::string extractGemmaCall(const std::string &s, std::string &rawObject)
{

    size_t pos = s.find("call:");
    if (pos == std::string::npos) return {};

    pos += 5;
    pos = skipWhitespace(s, pos);

    size_t nameStart = pos;
    while (pos < s.size() && (std::isalnum(static_cast<unsigned char>(s[pos])) || s[pos] == '_'))
        ++pos;
    if (pos == nameStart) return {};

    std::string name = s.substr(nameStart, pos - nameStart);

    pos = skipWhitespace(s, pos);
    if (pos >= s.size() || (s[pos] != '{' && s[pos] != '(')) return {};

    size_t objEnd = findValueEnd(s, pos);
    if (objEnd == std::string::npos) return {};

    rawObject = s.substr(pos, objEnd - pos);

    if (rawObject[0] == '(')
        rawObject = "{" + rawObject.substr(1, rawObject.size() - 2) + "}";
    return name;
}

inline std::string extractGemmaStringValue(const std::string &obj, const std::string &key)
{

    std::string search = key + ":";
    size_t pos = 0;
    while (true) {
        pos = obj.find(search, pos);
        if (pos == std::string::npos) return {};

        if (pos > 0 && (std::isalnum(static_cast<unsigned char>(obj[pos-1])) || obj[pos-1] == '_')) {
            pos += search.size();
            continue;
        }

        size_t valStart = skipWhitespace(obj, pos + search.size());
        if (valStart >= obj.size()) return {};

        if (obj[valStart] == '"' || obj[valStart] == '\'') {
            if (obj[valStart] == '"') {
                size_t valEnd = findStringEnd(obj, valStart);
                if (valEnd == std::string::npos) return {};
                return obj.substr(valStart + 1, valEnd - valStart - 2);
            } else {
                size_t valEnd = valStart + 1;
                while (valEnd < obj.size() && obj[valEnd] != '\'')
                    ++valEnd;
                if (valEnd >= obj.size()) return {};
                return obj.substr(valStart + 1, valEnd - valStart - 1);
            }
        }

        size_t valEnd = valStart;
        while (valEnd < obj.size() && obj[valEnd] != ',' && obj[valEnd] != '}' && obj[valEnd] != ' ' && obj[valEnd] != '\t')
            ++valEnd;
        if (valEnd > valStart)
            return obj.substr(valStart, valEnd - valStart);

        pos += search.size();
    }
}

inline std::string extractRawValue(const std::string &s, const std::string &key)
{

    std::string searchKey = "\"" + key + "\"";
    size_t pos = 0;
    while (true) {
        pos = s.find(searchKey, pos);
        if (pos == std::string::npos) break;

        size_t before = skipWhitespace(s, pos + searchKey.size());
        if (before >= s.size() || s[before] != ':') {
            pos += searchKey.size();
            continue;
        }
        size_t valStart = skipWhitespace(s, before + 1);
        size_t valEnd = findValueEnd(s, valStart);
        if (valEnd == std::string::npos) return {};
        return s.substr(valStart, valEnd - valStart);
    }

    std::string search = key + ":";
    pos = 0;
    while (true) {
        pos = s.find(search, pos);
        if (pos == std::string::npos) return {};

        if (pos > 0 && (std::isalnum(static_cast<unsigned char>(s[pos-1])) || s[pos-1] == '_')) {
            pos += search.size();
            continue;
        }

        size_t valStart = skipWhitespace(s, pos + search.size());
        size_t valEnd = findValueEnd(s, valStart);
        if (valEnd == std::string::npos) return {};
        return s.substr(valStart, valEnd - valStart);
    }
}

inline std::string extractToolArg(const std::string &rawArgs, const std::string &key)
{

    std::string val = extractStringValue(rawArgs, key);
    if (!val.empty()) return val;
    val = extractGemmaStringValue(rawArgs, key);
    if (!val.empty()) return val;

    std::string args = extractObjectValue(rawArgs, "arguments");
    if (args.empty())
        args = extractObjectValue(rawArgs, "parameters");
    if (!args.empty()) {
        val = extractStringValue(args, key);
        if (!val.empty()) return val;
        val = extractGemmaStringValue(args, key);
        if (!val.empty()) return val;
    }

    return {};
}

inline std::vector<std::string> extractStringArray(const std::string &s, const std::string &key)
{
    std::string val = extractRawValue(s, key);
    if (val.empty() || val[0] != '[') return {};

    std::vector<std::string> result;
    size_t i = 1;
    while (i < val.size()) {
        i = skipWhitespace(val, i);
        if (i >= val.size() || val[i] == ']') break;

        char quote = val[i];
        if (quote == '"' || quote == '\'') {
            ++i;
            std::string elem;
            while (i < val.size() && val[i] != quote) {
                if (val[i] == '\\' && i + 1 < val.size()) {
                    elem += val[i + 1];
                    i += 2;
                } else {
                    elem += val[i];
                    ++i;
                }
            }
            if (i < val.size()) ++i;
            result.push_back(elem);
        }

        while (i < val.size() && val[i] != ',' && val[i] != ']') ++i;
        if (i < val.size() && val[i] == ',') ++i;
    }

    return result;
}

}
