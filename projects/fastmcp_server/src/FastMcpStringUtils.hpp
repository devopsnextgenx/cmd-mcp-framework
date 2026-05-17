#pragma once

#include <string>

namespace fastmcp
{

class FastMcpStringUtils
{
public:
    static bool beginsWith(const std::string& value, const std::string& prefix);
    static bool endsWith(const std::string& value, const std::string& suffix);
    static std::string toUpperAscii(std::string value);
    static std::string toLowerAscii(std::string value);
    static std::string sanitizeToolName(std::string name);
};

} // namespace fastmcp
