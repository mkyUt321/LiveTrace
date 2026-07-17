#include "timing-correlator.h"

#include <algorithm>
#include <cmath>

namespace ns3
{
namespace livetrace
{

TimingCorrelator::TimingCorrelator(ObservationLog* obsLog, Config cfg)
    : m_obsLog(obsLog),
      m_cfg(cfg)
{
}

std::vector<int>
TimingCorrelator::ComputeOnOffSignal(const std::string& flowKey, double t0, double t1) const
{
    auto events = m_obsLog->EventsForFlow(flowKey, t0, t1);
    uint32_t numBuckets = static_cast<uint32_t>(std::ceil((t1 - t0) / m_cfg.bucketS)) + 1;
    std::vector<double> packetCount(numBuckets, 0.0);

    for (const auto& e : events)
    {
        int idx = static_cast<int>((e.timeS - t0) / m_cfg.bucketS);
        if (idx >= 0 && idx < static_cast<int>(numBuckets))
        {
            packetCount[idx] += 1.0;
        }
    }

    double onCountThreshold = std::max(1.0, m_cfg.onThresholdPps * m_cfg.bucketS);
    std::vector<int> signal(numBuckets, 0);
    for (uint32_t i = 0; i < numBuckets; ++i)
    {
        signal[i] = (packetCount[i] >= onCountThreshold) ? 1 : 0;
    }
    return signal;
}

uint32_t
TimingCorrelator::CountTransitions(const std::vector<int>& signal) const
{
    uint32_t transitions = 0;
    for (size_t i = 1; i < signal.size(); ++i)
    {
        if (signal[i] != signal[i - 1])
        {
            transitions++;
        }
    }
    return transitions;
}

namespace
{

double
PearsonAtLag(const std::vector<int>& a, const std::vector<int>& b, int lag)
{
    // b shifted by `lag` buckets relative to a; overlap region only.
    int n = static_cast<int>(a.size());
    int m = static_cast<int>(b.size());
    int start = std::max(0, -lag);
    int end = std::min(n, m - lag);
    if (end - start < 2)
    {
        return -1.0;
    }

    double sumA = 0, sumB = 0;
    int count = end - start;
    for (int i = start; i < end; ++i)
    {
        sumA += a[i];
        sumB += b[i + lag];
    }
    double meanA = sumA / count;
    double meanB = sumB / count;

    double cov = 0, varA = 0, varB = 0;
    for (int i = start; i < end; ++i)
    {
        double da = a[i] - meanA;
        double db = b[i + lag] - meanB;
        cov += da * db;
        varA += da * da;
        varB += db * db;
    }
    if (varA <= 0.0 || varB <= 0.0)
    {
        return -1.0;
    }
    return cov / std::sqrt(varA * varB);
}

} // namespace

double
TimingCorrelator::Correlate(const std::vector<int>& a, const std::vector<int>& b) const
{
    if (CountTransitions(a) < m_cfg.minOnOffTransitions || CountTransitions(b) < m_cfg.minOnOffTransitions)
    {
        return -1.0;
    }
    double best = -1.0;
    for (int lag = -m_cfg.maxLagBuckets; lag <= m_cfg.maxLagBuckets; ++lag)
    {
        double score = PearsonAtLag(a, b, lag);
        best = std::max(best, score);
    }
    return best;
}

TimingCorrelator::Match
TimingCorrelator::FindBestUpstreamMatch(const std::string& referenceFlowKey,
                                         uint32_t candidateNodeId,
                                         double t0,
                                         double t1,
                                         double scoreThreshold,
                                         const std::vector<std::string>& excludeFlowKeys) const
{
    Match result{"", -1.0, false};
    std::vector<int> refSignal = ComputeOnOffSignal(referenceFlowKey, t0, t1);

    for (const auto& flowKey : m_obsLog->FlowsAtNode(candidateNodeId, t0, t1))
    {
        if (flowKey == referenceFlowKey)
        {
            continue;
        }
        if (std::find(excludeFlowKeys.begin(), excludeFlowKeys.end(), flowKey) != excludeFlowKeys.end())
        {
            continue;
        }
        // Only consider flows where candidateNodeId is the receiving side
        // (i.e. it is the destination of this connection) -- that's the set
        // of "who might be feeding this relay" hypotheses.
        auto events = m_obsLog->EventsForFlow(flowKey, t0, t1);
        bool hasRxHere = false;
        for (const auto& e : events)
        {
            if (e.nodeId == candidateNodeId && e.isRx)
            {
                hasRxHere = true;
                break;
            }
        }
        if (!hasRxHere)
        {
            continue;
        }

        std::vector<int> candSignal = ComputeOnOffSignal(flowKey, t0, t1);
        double score = Correlate(refSignal, candSignal);
        if (score > result.score)
        {
            result.score = score;
            result.flowKey = flowKey;
        }
    }

    result.valid = (result.score >= scoreThreshold);
    return result;
}

} // namespace livetrace
} // namespace ns3
