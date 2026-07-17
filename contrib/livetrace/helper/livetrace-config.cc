#include "livetrace-config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace ns3
{
namespace livetrace
{

namespace
{

std::string
Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
    {
        return "";
    }
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string
StripComment(const std::string& line)
{
    // '#' only strips comments outside of list brackets/quotes; our config
    // never quotes strings, so a plain search is sufficient.
    size_t pos = line.find('#');
    if (pos == std::string::npos)
    {
        return line;
    }
    return line.substr(0, pos);
}

int
IndentOf(const std::string& raw)
{
    int n = 0;
    for (char c : raw)
    {
        if (c == ' ')
        {
            n++;
        }
        else
        {
            break;
        }
    }
    return n;
}

} // namespace

bool
LiveTraceConfig::Load(const std::string& path)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        return false;
    }

    std::string line;
    std::string currentSection;
    while (std::getline(in, line))
    {
        std::string stripped = StripComment(line);
        if (Trim(stripped).empty())
        {
            continue;
        }
        int indent = IndentOf(stripped);
        std::string content = Trim(stripped);

        size_t colon = content.find(':');
        if (colon == std::string::npos)
        {
            continue;
        }
        std::string key = Trim(content.substr(0, colon));
        std::string value = Trim(content.substr(colon + 1));

        if (indent == 0)
        {
            if (value.empty())
            {
                // Start of a nested section.
                currentSection = key;
                continue;
            }
            currentSection.clear();
            m_values[key] = value;
        }
        else
        {
            std::string fullKey = currentSection.empty() ? key : currentSection + "." + key;
            m_values[fullKey] = value;
        }
    }
    return true;
}

void
LiveTraceConfig::Override(const std::string& dottedKey, const std::string& value)
{
    m_values[dottedKey] = value;
}

std::string
LiveTraceConfig::GetString(const std::string& dottedKey, const std::string& def) const
{
    auto it = m_values.find(dottedKey);
    if (it == m_values.end())
    {
        return def;
    }
    return it->second;
}

double
LiveTraceConfig::GetDouble(const std::string& dottedKey, double def) const
{
    auto it = m_values.find(dottedKey);
    if (it == m_values.end())
    {
        return def;
    }
    try
    {
        return std::stod(it->second);
    }
    catch (...)
    {
        return def;
    }
}

int
LiveTraceConfig::GetInt(const std::string& dottedKey, int def) const
{
    auto it = m_values.find(dottedKey);
    if (it == m_values.end())
    {
        return def;
    }
    try
    {
        return std::stoi(it->second);
    }
    catch (...)
    {
        return def;
    }
}

bool
LiveTraceConfig::GetBool(const std::string& dottedKey, bool def) const
{
    auto it = m_values.find(dottedKey);
    if (it == m_values.end())
    {
        return def;
    }
    std::string v = it->second;
    std::transform(v.begin(), v.end(), v.begin(), ::tolower);
    return v == "true" || v == "1" || v == "yes";
}

std::vector<double>
LiveTraceConfig::GetDoubleList(const std::string& dottedKey) const
{
    std::vector<double> out;
    auto it = m_values.find(dottedKey);
    if (it == m_values.end())
    {
        return out;
    }
    std::string v = it->second;
    // Expect "[a, b, c]".
    size_t open = v.find('[');
    size_t close = v.find(']');
    if (open == std::string::npos || close == std::string::npos)
    {
        return out;
    }
    std::string inner = v.substr(open + 1, close - open - 1);
    std::stringstream ss(inner);
    std::string tok;
    while (std::getline(ss, tok, ','))
    {
        std::string t = Trim(tok);
        if (!t.empty())
        {
            try
            {
                out.push_back(std::stod(t));
            }
            catch (...)
            {
            }
        }
    }
    return out;
}

std::vector<int>
LiveTraceConfig::GetIntList(const std::string& dottedKey) const
{
    std::vector<int> out;
    for (double d : GetDoubleList(dottedKey))
    {
        out.push_back(static_cast<int>(d));
    }
    return out;
}

const std::map<std::string, std::string>&
LiveTraceConfig::Raw() const
{
    return m_values;
}

} // namespace livetrace
} // namespace ns3
