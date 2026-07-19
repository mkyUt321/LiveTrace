#include "observation-log.h"

#include <algorithm>
#include <unordered_set>

namespace ns3
{
namespace livetrace
{

namespace
{

/// Inserts `e` into `bucket` at its sorted-by-timeS position, keeping the
/// bucket fully sorted at all times so windowed queries can binary-search
/// instead of scanning the whole history and re-sorting on every call.
/// Simulated production traffic is always recorded at the current simulated
/// instant (Simulator::Now() is non-decreasing across the whole run), so in
/// practice `pos` is almost always bucket.end() -- amortized O(1), same cost
/// as push_back. The binary search + possible mid-vector shift only matter
/// (at negligible absolute cost, given how small these fixtures are) when
/// events are appended out of chronological order, as some unit tests do
/// when hand-building synthetic logs with explicit timestamps.
void
InsertSorted(std::vector<ObservationLog::Event>& bucket, const ObservationLog::Event& e)
{
    auto pos = std::upper_bound(bucket.begin(), bucket.end(), e.timeS,
                                 [](double t, const ObservationLog::Event& x) { return t < x.timeS; });
    bucket.insert(pos, e);
}

/// Extracts the [t0, t1] window from an already-sorted-by-timeS bucket via
/// binary search, instead of a full linear scan.
std::vector<ObservationLog::Event>
WindowedCopy(const std::vector<ObservationLog::Event>& sortedEvents, double t0, double t1)
{
    auto begin = std::lower_bound(sortedEvents.begin(), sortedEvents.end(), t0,
                                   [](const ObservationLog::Event& e, double t) { return e.timeS < t; });
    std::vector<ObservationLog::Event> out;
    for (auto it = begin; it != sortedEvents.end() && it->timeS <= t1; ++it)
    {
        out.push_back(*it);
    }
    return out;
}

} // namespace

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
    InsertSorted(m_byNode[e.nodeId], e);
    InsertSorted(m_byFlow[e.flowKey], e);
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
    auto it = m_byNode.find(nodeId);
    if (it == m_byNode.end())
    {
        return {};
    }
    return WindowedCopy(it->second, t0, t1);
}

std::vector<std::string>
ObservationLog::FlowsAtNode(uint32_t nodeId, double t0, double t1) const
{
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const auto& e : EventsAtNode(nodeId, t0, t1))
    {
        if (seen.insert(e.flowKey).second)
        {
            out.push_back(e.flowKey);
        }
    }
    return out;
}

std::vector<ObservationLog::Event>
ObservationLog::EventsForFlow(const std::string& flowKey, double t0, double t1) const
{
    auto it = m_byFlow.find(flowKey);
    if (it == m_byFlow.end())
    {
        return {};
    }
    return WindowedCopy(it->second, t0, t1);
}

} // namespace livetrace
} // namespace ns3
