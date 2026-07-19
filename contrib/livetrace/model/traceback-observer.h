#ifndef LIVETRACE_TRACEBACK_OBSERVER_H
#define LIVETRACE_TRACEBACK_OBSERVER_H

#include "node-address-index.h"
#include "observation-log.h"
#include "timing-correlator.h"

#include "ns3/ipv4-address.h"

#include <cstdint>
#include <fstream>
#include <functional>
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
        double accumulationDelayS; // wait this long after first packet before the first correlation
        double hopDelayS;          // additional wait before each subsequent hop's correlation
        uint32_t maxHops;
        double scoreThreshold;
    };

    /// traceId, burstDetectTimeS, finalChain (victim-first), stopReason,
    /// hop0FlowKey (the victim-inbound flow that seeded this trace -- the
    /// most directly cross-burst-comparable signal for re-identification),
    /// lastConfirmedFlowKey (the flow the walk was still following when it
    /// stopped -- lets a caller resolve the physical route into the final
    /// node, e.g. for NetAnim's L3 overlay).
    using TraceCompleteFn = std::
        function<void(uint32_t, double, std::vector<uint32_t>, std::string, std::string, std::string)>;

    /// traceId, nodeId just confirmed, hopsSoFar, eval time, downstreamFlowKey
    /// (the flow this hop was matched from -- lets a caller resolve the
    /// physical route between this node and the previously confirmed one).
    /// Fired at every successful hop (not just trace completion). Used to
    /// drive NetAnim node highlighting live as the trace progresses (Phase 5).
    using HopConfirmedFn = std::function<void(uint32_t, uint32_t, uint32_t, double, std::string)>;

    /// traceId, timeS -- fired the moment a new trace begins (a fresh flow
    /// arrives at the victim). Used to reset per-burst NetAnim highlighting
    /// before the new trace's hops start lighting up.
    using TraceStartedFn = std::function<void(uint32_t, double)>;

    TracebackObserver(ObservationLog* obsLog,
                       NodeAddressIndex* addrIndex,
                       TimingCorrelator* correlator,
                       uint32_t victimNodeId,
                       Config cfg,
                       const std::string& outPath);

    /// False if the output file at `outPath` could not be opened -- callers
    /// should treat this as fatal rather than silently losing traceback logs.
    bool Ok() const
    {
        return m_out.is_open();
    }

    /// Hook this to the victim sink's StepstoneRelayApp::SetRecvNotify.
    void OnFlowObserved(uint32_t nodeId, std::string flowKey, double timeS, Ipv4Address peerAddr);

    /// Fired once per trace when it stops for any reason (used by
    /// ReidentificationEngine to consume each finished traceback).
    void SetTraceCompleteNotify(TraceCompleteFn fn);

    /// Fired on every confirmed hop, as it happens.
    void SetHopConfirmedNotify(HopConfirmedFn fn);

    /// Fired once per trace when it starts (before its first hop).
    void SetTraceStartedNotify(TraceStartedFn fn);

  private:
    void AttemptTrace(uint32_t traceId,
                       std::string hop0FlowKey,
                       std::string confirmedFlowKey,
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
    uint32_t m_nextTraceId;
    TraceCompleteFn m_traceComplete;
    HopConfirmedFn m_hopConfirmed;
    TraceStartedFn m_traceStarted;
};

} // namespace livetrace
} // namespace ns3

#endif // LIVETRACE_TRACEBACK_OBSERVER_H
