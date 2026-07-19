#ifndef LIVETRACE_OBSERVATION_LOG_H
#define LIVETRACE_OBSERVATION_LOG_H

#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace ns3
{
namespace livetrace
{

/**
 * Observable-traffic log: every packet send/receive event that a
 * network-monitoring vantage point at a node could plausibly see (5-tuple-ish
 * flow key, direction, timestamp, size). This is the ONLY input the
 * correlator / traceback observer / re-identification engine are allowed to
 * read. It carries no notion of which flows are truly chained or attacker
 * traffic -- that separation is what makes the traceback problem real rather
 * than a lookup.
 */
class ObservationLog
{
  public:
    struct Event
    {
        double timeS;
        uint32_t nodeId;
        bool isRx; // true = receive, false = send
        std::string flowKey;
        uint32_t sizeBytes;
    };

    explicit ObservationLog(const std::string& persistPath = "");
    ~ObservationLog();

    /// True unless a persist path was given but couldn't be opened -- an
    /// ObservationLog with no persist path at all (the common case in unit
    /// tests) is fine and reports true. Callers that did request persistence
    /// should treat false as fatal rather than silently running with the
    /// observation log going nowhere.
    bool Ok() const
    {
        return !m_persistRequested || m_persist.is_open();
    }

    void RecordSend(uint32_t nodeId, const std::string& flowKey, double timeS, uint32_t sizeBytes);
    void RecordRecv(uint32_t nodeId, const std::string& flowKey, double timeS, uint32_t sizeBytes);

    /// All events observed at a node within [t0, t1] (inclusive), time-ordered.
    std::vector<Event> EventsAtNode(uint32_t nodeId, double t0, double t1) const;

    /// Distinct flow keys with at least one event at a node within [t0, t1].
    std::vector<std::string> FlowsAtNode(uint32_t nodeId, double t0, double t1) const;

    /// All events for one flow key within [t0, t1], time-ordered.
    std::vector<Event> EventsForFlow(const std::string& flowKey, double t0, double t1) const;

  private:
    void Record(const Event& e);

    std::map<uint32_t, std::vector<Event>> m_byNode;
    std::map<std::string, std::vector<Event>> m_byFlow;
    std::ofstream m_persist;
    bool m_persistRequested = false;
};

} // namespace livetrace
} // namespace ns3

#endif // LIVETRACE_OBSERVATION_LOG_H
