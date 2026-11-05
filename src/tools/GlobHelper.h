#pragma once

#include <string>

namespace glob_helper {

// Converts a simple glob to an ECMAScript regex. Supports '*', '?',
// character classes and brace alternatives such as "*.{h,cpp}".
inline std::string globToRegex(const std::string &glob)
{
    std::string re;
    re.reserve(glob.size() + 8);

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
            case '^': re += "\\^"; break;
            case '$': re += "\\$"; break;
            case '|': re += "\\|"; break;
            case '\\': re += "\\\\"; break;
            case '{': {
                size_t end = i + 1;
                int depth = 1;
                for (; end < glob.size(); ++end) {
                    if (glob[end] == '{') ++depth;
                    else if (glob[end] == '}' && --depth == 0) break;
                }
                if (end >= glob.size()) {
                    re += "\\{";
                    break;
                }

                std::string body = glob.substr(i + 1, end - i - 1);
                re += "(?:";

                size_t start = 0;
                int innerDepth = 0;
                bool first = true;
                for (size_t k = 0; k <= body.size(); ++k) {
                    if (k == body.size() || (body[k] == ',' && innerDepth == 0)) {
                        if (!first) re += "|";
                        first = false;
                        re += globToRegex(body.substr(start, k - start));
                        start = k + 1;
                    } else if (body[k] == '{') {
                        ++innerDepth;
                    } else if (body[k] == '}' && innerDepth > 0) {
                        --innerDepth;
                    }
                }

                re += ")";
                i = end;
                break;
            }
            case '}': re += "\\}"; break;
            default:  re += c;     break;
        }
    }

    return re;
}

} // namespace glob_helper
