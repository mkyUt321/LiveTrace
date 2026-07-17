#include "oracle-logger.h"

#include "ns3/simulator.h"

namespace ns3
{
namespace livetrace
{

OracleLogger::OracleLogger(const std::string& path)
    : m_out(path)
{
}

OracleLogger::~OracleLogger()
{
    if (m_out.is_open())
    {
        m_out.close();
    }
}

void
OracleLogger::LogBurst(uint32_t burstId,
                        uint32_t trueActorId,
                        double startTimeS,
                        const std::vector<uint32_t>& trueChainNodeIds)
{
    if (!m_out.is_open())
    {
        return;
    }
    m_out << "{\"type\":\"burst\",\"burst_id\":" << burstId << ",\"true_actor_id\":" << trueActorId
          << ",\"start_time_s\":" << startTimeS << ",\"true_chain\":[";
    for (size_t i = 0; i < trueChainNodeIds.size(); ++i)
    {
        m_out << trueChainNodeIds[i];
        if (i + 1 < trueChainNodeIds.size())
        {
            m_out << ",";
        }
    }
    m_out << "]}" << std::endl;
}

void
OracleLogger::LogTopologyNote(const std::string& note)
{
    if (!m_out.is_open())
    {
        return;
    }
    m_out << "{\"type\":\"topology_note\",\"note\":\"" << note << "\"}" << std::endl;
}

} // namespace livetrace
} // namespace ns3
