#include "observation-log.h"

#include <algorithm>

namespace ns3
{
namespace livetrace
{

ObservationLog::ObservationLog(const std::string& persistPath)
    : m_persistRequested(!persistPath.empty())
{
    if (m_persistRequested)
    {
        m_persist.open(persistPath);
    }
}

ObservationLog::~ObservationLog()
{
    if (m_persist.is_open())
    {
        m_persist.close();
    }
}

void
ObservationLog::Record(const Event& e)
{
    m_byNode[e.nodeId].push_back(e);
    m_byFlow[e.flowKey].push_back(e);
    if (m_persist.is_open())
    {
        m_persist << "{\"t\":" << e.timeS << ",\"node\":" << e.nodeId
                  << ",\"dir\":\"" << (e.isRx ? "rx" : "tx") << "\",\"flow\":\"" << e.flowKey
                  << "\",\"bytes\":" << e.sizeBytes << "}" << std::endl;
    }
}

void
ObservationLog::RecordSend(uint32_t nodeId, const std::string& flowKey, double timeS, uint32_t sizeBytes)
{
    Record(Event{timeS, nodeId, false, flowKey, sizeBytes});
}

void
ObservationLog::RecordRecv(uint32_t nodeId, const std::string& flowKey, double timeS, uint32_t sizeBytes)
{
    Record(Event{timeS, nodeId, true, flowKey, sizeBytes});
}

std::vector<ObservationLog::Event>
ObservationLog::EventsAtNode(uint32_t nodeId, double t0, double t1) const
{
    std::vector<Event> out;
    auto it = m_byNode.find(nodeId);
    if (it == m_byNode.end())
    {
        return out;
    }
    for (const auto& e : it->second)
    {
        if (e.timeS >= t0 && e.timeS <= t1)
        {
            out.push_back(e);
        }
    }
    std::sort(out.begin(), out.end(), [](const Event& a, const Event& b) { return a.timeS < b.timeS; });
    return out;
}

std::vector<std::string>
ObservationLog::FlowsAtNode(uint32_t nodeId, double t0, double t1) const
{
    std::vector<std::string> out;
    for (const auto& e : EventsAtNode(nodeId, t0, t1))
    {
        if (std::find(out.begin(), out.end(), e.flowKey) == out.end())
        {
            out.push_back(e.flowKey);
        }
    }
    return out;
}

std::vector<ObservationLog::Event>
ObservationLog::EventsForFlow(const std::string& flowKey, double t0, double t1) const
{
    std::vector<Event> out;
    auto it = m_byFlow.find(flowKey);
    if (it == m_byFlow.end())
    {
        return out;
    }
    for (const auto& e : it->second)
    {
        if (e.timeS >= t0 && e.timeS <= t1)
        {
            out.push_back(e);
        }
    }
    std::sort(out.begin(), out.end(), [](const Event& a, const Event& b) { return a.timeS < b.timeS; });
    return out;
}

} // namespace livetrace
} // namespace ns3
