#ifndef LIVETRACE_CONFIG_H
#define LIVETRACE_CONFIG_H

#include <map>
#include <string>
#include <vector>

namespace ns3
{
namespace livetrace
{

/**
 * Restricted-subset YAML reader: flat scalars plus up to two levels of
 * nesting (`section:` / `  key: value`) and single-line lists (`key: [a, b]`).
 * The project's config/default.yaml is written to stay inside this subset so
 * the same file is also fully readable by PyYAML on the analysis side.
 */
class LiveTraceConfig
{
  public:
    LiveTraceConfig() = default;

    /// Load and parse a config file. Returns false on I/O failure.
    bool Load(const std::string& path);

    /// Apply a single "section.key=value" override (e.g. from CommandLine).
    void Override(const std::string& dottedKey, const std::string& value);

    std::string GetString(const std::string& dottedKey, const std::string& def = "") const;
    double GetDouble(const std::string& dottedKey, double def = 0.0) const;
    int GetInt(const std::string& dottedKey, int def = 0) const;
    bool GetBool(const std::string& dottedKey, bool def = false) const;
    std::vector<double> GetDoubleList(const std::string& dottedKey) const;
    std::vector<int> GetIntList(const std::string& dottedKey) const;

    /// All parsed key/value pairs, keys as "section.key".
    const std::map<std::string, std::string>& Raw() const;

  private:
    std::map<std::string, std::string> m_values;
};

} // namespace livetrace
} // namespace ns3

#endif // LIVETRACE_CONFIG_H
