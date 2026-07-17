#ifndef LIVETRACE_TRACEBACK_OBSERVER_H
#define LIVETRACE_TRACEBACK_OBSERVER_H

#include "node-address-index.h"
#include "observation-log.h"
#include "timing-correlator.h"

#include "ns3/ipv4-address.h"

#include <cstdint>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace ns3
{
namespace livetrace
{

/**
 * Online hop-by-hop traceback: when a new flow starts at a watched node
 * (initially the victim), search that node's confirmed peer for whichever
 * of ITS candidate inbound flows best time-correlates with the flow that
 * fed the node we just came from, then move to that peer and repeat --
 * entirely from ObservationLog + NodeAddressIndex (public address book).
 * Never reads OracleLogger or any ground truth.
 *
 * Phase 1 scope: a single hop (maxHops = 1) to prove the correlation step
 * in isolation. Phase 2 lifts the cap into full recursive hop-by-hop
 * traceback with live-window budget tracking and origin detection.
 */
class TracebackObserver
{
  public:
    struct Config
    {
        double liveWindowS;
        double accumulationDelayS; // wait this long after first packet before correlating
        uint32_t maxHops;
        double scoreThreshold;
    };

    TracebackObserver(ObservationLog* obsLog,
                       NodeAddressIndex* addrIndex,
                       TimingCorrelator* correlator,
                       uint32_t victimNodeId,
                       Config cfg,
                       const std::string& outPath);

    /// Hook this to the victim sink's StepstoneRelayApp::SetRecvNotify.
    void OnFlowObserved(uint32_t nodeId, std::string flowKey, double timeS, Ipv4Address peerAddr);

  private:
    void AttemptTrace(std::string confirmedFlowKey,
                       double burstDetectTimeS,
                       uint32_t hopsSoFar,
                       std::vector<uint32_t> chainSoFar);

    ObservationLog* m_obsLog;
    NodeAddressIndex* m_addrIndex;
    TimingCorrelator* m_correlator;
    uint32_t m_victimNodeId;
    Config m_cfg;
    std::ofstream m_out;
    std::set<std::string> m_seenAtVictim;
    uint32_t m_nextAttemptId;
};

} // namespace livetrace
} // namespace ns3

#endif // LIVETRACE_TRACEBACK_OBSERVER_H
