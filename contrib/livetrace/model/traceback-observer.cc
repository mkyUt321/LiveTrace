#include "traceback-observer.h"

#include "flow-key-util.h"

#include "ns3/simulator.h"

namespace ns3
{
namespace livetrace
{

TracebackObserver::TracebackObserver(ObservationLog* obsLog,
                                      NodeAddressIndex* addrIndex,
                                      TimingCorrelator* correlator,
                                      uint32_t victimNodeId,
                                      Config cfg,
                                      const std::string& outPath)
    : m_obsLog(obsLog),
      m_addrIndex(addrIndex),
      m_correlator(correlator),
      m_victimNodeId(victimNodeId),
      m_cfg(cfg),
      m_out(outPath),
      m_nextAttemptId(0)
{
}

void
TracebackObserver::OnFlowObserved(uint32_t nodeId, std::string flowKey, double timeS, Ipv4Address peerAddr)
{
    if (nodeId != m_victimNodeId)
    {
        return;
    }
    if (m_seenAtVictim.count(flowKey) > 0)
    {
        return; // already tracing (or traced) this flow
    }
    m_seenAtVictim.insert(flowKey);

    Simulator::Schedule(Seconds(m_cfg.accumulationDelayS),
                         &TracebackObserver::AttemptTrace,
                         this,
                         flowKey,
                         timeS,
                         0u,
                         std::vector<uint32_t>{m_victimNodeId});
}

void
TracebackObserver::AttemptTrace(std::string confirmedFlowKey,
                                 double burstDetectTimeS,
                                 uint32_t hopsSoFar,
                                 std::vector<uint32_t> chainSoFar)
{
    uint32_t attemptId = m_nextAttemptId++;
    double now = Simulator::Now().GetSeconds();

    auto writeResult = [&](const std::string& stopReason, const std::string& matchedFlow, double score) {
        m_out << "{\"attempt_id\":" << attemptId << ",\"burst_detect_time_s\":" << burstDetectTimeS
              << ",\"hops_so_far\":" << hopsSoFar << ",\"chain_so_far\":[";
        for (size_t i = 0; i < chainSoFar.size(); ++i)
        {
            m_out << chainSoFar[i];
            if (i + 1 < chainSoFar.size())
            {
                m_out << ",";
            }
        }
        m_out << "],\"stop_reason\":\"" << stopReason << "\",\"matched_flow\":\"" << matchedFlow
              << "\",\"score\":" << score << ",\"eval_time_s\":" << now << "}" << std::endl;
    };

    Ipv4Address peerAddr = FlowKeySideAddr(confirmedFlowKey, /*wantSrc=*/true);
    uint32_t peerNode = m_addrIndex->Lookup(peerAddr);
    if (peerNode == NodeAddressIndex::kNotFound)
    {
        writeResult("unknown_peer_address", "", -1.0);
        return;
    }
    chainSoFar.push_back(peerNode);

    double deadline = burstDetectTimeS + m_cfg.liveWindowS;
    if (now > deadline)
    {
        writeResult("window_expired", "", -1.0);
        return;
    }
    if (hopsSoFar >= m_cfg.maxHops)
    {
        writeResult("hop_limit_reached", "", -1.0);
        return;
    }

    TimingCorrelator::Match match =
        m_correlator->FindBestUpstreamMatch(confirmedFlowKey, peerNode, burstDetectTimeS, now, m_cfg.scoreThreshold,
                                             {confirmedFlowKey});

    if (!match.valid)
    {
        writeResult("no_match_above_threshold", match.flowKey, match.score);
        return;
    }

    writeResult("matched", match.flowKey, match.score);
    // Phase 2 extends this: recurse into AttemptTrace(match.flowKey, burstDetectTimeS,
    // hopsSoFar + 1, chainSoFar) with live-window budget and origin detection.
}

} // namespace livetrace
} // namespace ns3
