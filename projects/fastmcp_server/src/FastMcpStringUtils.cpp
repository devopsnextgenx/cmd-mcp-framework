#include "FastMcpStringUtils.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

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

    std::string FastMcpStringUtils::nowUtcIso8601()
    {
        const auto now = std::chrono::system_clock::now();
        const auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#if defined(_WIN32)
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

    std::string FastMcpStringUtils::safeSessionId(const std::optional<std::string>& session_id)
    {
        return session_id.has_value() ? *session_id : "<none>";
    }

    bool FastMcpStringUtils::uriToMcpAppsPath(const std::string& uri, std::string& path)
    {
        if (uri.empty()) return false;
        if (FastMcpStringUtils::beginsWith(uri, "ui://"))
        {
            std::string p = uri.substr(5);
            if (FastMcpStringUtils::beginsWith(p, "ui/"))
            {
                path = "/" + p;
                return true;
            }
        }
        return false;
    }

    std::string FastMcpStringUtils::toMcpAppUri(const std::string& uri)
    {
        std::string path;
        if (!FastMcpStringUtils::uriToMcpAppsPath(uri, path)) return uri;
        return (path == "/") ? "ui://dashboard-ui" : "ui://" + path.substr(1);
    }

    std::string FastMcpStringUtils::protocolModeToString(ProtocolMode mode)
    {
        switch (mode)
        {
        case ProtocolMode::MCP_ONLY:  return "MCP Only";
        case ProtocolMode::REST_ONLY: return "REST Only";
        default:                       return "MCP + REST";
        }
    }

} // namespace fastmcp
