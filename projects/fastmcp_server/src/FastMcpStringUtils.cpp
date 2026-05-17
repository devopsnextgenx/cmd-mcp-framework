#include "FastMcpStringUtils.hpp"

#include <algorithm>
#include <cctype>

namespace fastmcp
{

    bool FastMcpStringUtils::beginsWith(const std::string& value, const std::string& prefix)
    {
        return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
    }

    bool FastMcpStringUtils::endsWith(const std::string& value, const std::string& suffix)
    {
        return value.size() >= suffix.size() &&
            value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    std::string FastMcpStringUtils::toUpperAscii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return value;
    }

    std::string FastMcpStringUtils::toLowerAscii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    std::string FastMcpStringUtils::sanitizeToolName(std::string name)
    {
        std::string out;
        out.reserve(name.size());

        bool last_was_sep = false;
        for (const unsigned char c : name)
        {
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
            {
                out.push_back(static_cast<char>(c));
                last_was_sep = false;
                continue;
            }

            if (c >= 'A' && c <= 'Z')
            {
                out.push_back(static_cast<char>(std::tolower(c)));
                last_was_sep = false;
                continue;
            }

            if (c == '.' || c == '/' || std::isspace(c))
            {
                if (!last_was_sep && !out.empty())
                {
                    out.push_back('_');
                    last_was_sep = true;
                }
            }
        }

        while (!out.empty() && (out.back() == '_' || out.back() == '-')) out.pop_back();
        if (out.empty()) return "tool";
        return out;
    }

} // namespace fastmcp
