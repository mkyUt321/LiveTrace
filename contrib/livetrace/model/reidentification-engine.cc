#include "reidentification-engine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ns3
{
namespace livetrace
{

ReidentificationEngine::ReidentificationEngine(ObservationLog* obsLog,
                                                TimingCorrelator* correlator,
                                                Config cfg,
                                                const std::string& outPath)
    : m_obsLog(obsLog),
      m_correlator(correlator),
      m_cfg(cfg),
      m_out(outPath),
      m_nextClusterId(0)
{
}

ReidentificationEngine::Fingerprint
ReidentificationEngine::ComputeFingerprint(const std::string& flowKey, double t0, double t1) const
{
    std::vector<int> signal = m_correlator->ComputeOnOffSignal(flowKey, t0, t1);
    uint32_t transitions = 0;
    uint32_t onRuns = 0;
    uint32_t onBuckets = 0;
    for (size_t i = 0; i < signal.size(); ++i)
    {
        if (signal[i] == 1)
        {
            onBuckets++;
            if (i == 0 || signal[i - 1] == 0)
            {
                onRuns++;
            }
        }
        if (i > 0 && signal[i] != signal[i - 1])
        {
            transitions++;
        }
    }
    // Bucket width isn't known here directly; approximate mean on-duration in
    // bucket units (scaled callers only ever compare it against other
    // fingerprints computed the same way, so absolute units don't matter).
    double meanOnDuration = onRuns > 0 ? static_cast<double>(onBuckets) / onRuns : 0.0;
    return Fingerprint{meanOnDuration, transitions};
}

double
ReidentificationEngine::ScoreAgainstCluster(const Cluster& c,
                                             double detectTimeS,
                                             const std::set<uint32_t>& relays,
                                             const std::string& hop0FlowKey,
                                             double t0,
                                             double t1,
                                             const Fingerprint& fp) const
{
    // Periodicity: fit against the cluster's own estimate once it has one,
    // the configured prior otherwise, allowing for a small number of missed
    // detections in between.
    double period = c.estimatedPeriodS > 0.0 ? c.estimatedPeriodS : m_cfg.periodPriorS;
    double bestDiff = std::numeric_limits<double>::max();
    for (int k = 1; k <= 3; ++k)
    {
        double predicted = c.lastDetectTimeS + k * period;
        bestDiff = std::min(bestDiff, std::abs(detectTimeS - predicted));
    }
    double periodicityScore = std::max(0.0, 1.0 - bestDiff / m_cfg.periodicityToleranceS);

    // Path convergence: overlap of this burst's relay nodes with the
    // cluster's previously-seen relays.
    double convergenceScore = 0.0;
    if (!relays.empty() || !c.relaysSeen.empty())
    {
        std::vector<uint32_t> inter;
        std::set_intersection(relays.begin(), relays.end(), c.relaysSeen.begin(), c.relaysSeen.end(),
                               std::back_inserter(inter));
        std::vector<uint32_t> uni;
        std::set_union(relays.begin(), relays.end(), c.relaysSeen.begin(), c.relaysSeen.end(),
                        std::back_inserter(uni));
        convergenceScore = uni.empty() ? 0.0 : static_cast<double>(inter.size()) / uni.size();
    }
    // Periodicity is the more reliable of the two primary signals in
    // practice (a fixed inter-burst period vs. convergence over a randomly
    // sampled relay pool, which two same-actor bursts may only partially
    // overlap on by chance), so it carries more weight within the combined
    // primary score.
    double primaryScore = 0.7 * periodicityScore + 0.3 * convergenceScore;

    // Timing correlation: on/off signal shape similarity against the
    // cluster's reference burst (same mechanism as hop-by-hop traceback).
    std::vector<int> refSignal = m_correlator->ComputeOnOffSignal(c.refFlowKey, c.refWindowStart, c.refWindowEnd);
    std::vector<int> newSignal = m_correlator->ComputeOnOffSignal(hop0FlowKey, t0, t1);
    double timingScoreRaw = m_correlator->Correlate(refSignal, newSignal);
    double timingScore = std::max(0.0, timingScoreRaw); // Correlate returns [-1,1] or -1 sentinel

    // Fingerprint: closeness of on-duration and transition-count statistics.
    double onDurSim =
        1.0 - std::min(1.0, std::abs(fp.meanOnDurationS - c.fingerprintOnDurationAvg) /
                                 std::max(0.5, c.fingerprintOnDurationAvg));
    double transSim = 1.0 - std::min(1.0, std::abs(static_cast<double>(fp.transitions) - c.fingerprintTransitionsAvg) /
                                               std::max(1.0, c.fingerprintTransitionsAvg));
    double fingerprintScore = 0.5 * (onDurSim + transSim);

    return m_cfg.periodicityWeight * primaryScore + m_cfg.timingWeight * timingScore +
           m_cfg.fingerprintWeight * fingerprintScore;
}

void
ReidentificationEngine::OnTraceComplete(uint32_t traceId,
                                        double burstDetectTimeS,
                                        std::vector<uint32_t> chain,
                                        std::string stopReason,
                                        std::string hop0FlowKey)
{
    if (chain.size() < 2)
    {
        return;
    }
    std::set<uint32_t> relays;
    if (chain.size() > 2)
    {
        relays.insert(chain.begin() + 1, chain.end() - 1);
    }

    double t0 = burstDetectTimeS;
    double t1 = burstDetectTimeS + m_cfg.evidenceWindowS;
    Fingerprint fp = ComputeFingerprint(hop0FlowKey, t0, t1);

    int bestCluster = -1;
    double bestScore = -1.0;
    for (size_t i = 0; i < m_clusters.size(); ++i)
    {
        double score = ScoreAgainstCluster(m_clusters[i], burstDetectTimeS, relays, hop0FlowKey, t0, t1, fp);
        if (score > bestScore)
        {
            bestScore = score;
            bestCluster = static_cast<int>(i);
        }
    }

    uint32_t assignedClusterId;
    double loggedScore;
    if (bestCluster >= 0 && bestScore >= m_cfg.clusterScoreThreshold)
    {
        Cluster& c = m_clusters[bestCluster];
        double newInterval = burstDetectTimeS - c.lastDetectTimeS;
        double priorPeriod = c.estimatedPeriodS > 0.0 ? c.estimatedPeriodS : m_cfg.periodPriorS;
        int k = std::max(1, static_cast<int>(std::round(newInterval / priorPeriod)));
        double adjustedInterval = newInterval / k;
        c.estimatedPeriodS = (c.estimatedPeriodS <= 0.0) ? adjustedInterval
                                                          : 0.7 * c.estimatedPeriodS + 0.3 * adjustedInterval;
        c.lastDetectTimeS = burstDetectTimeS;
        c.memberCount++;
        c.relaysSeen.insert(relays.begin(), relays.end());
        c.fingerprintOnDurationAvg += (fp.meanOnDurationS - c.fingerprintOnDurationAvg) / c.memberCount;
        c.fingerprintTransitionsAvg +=
            (static_cast<double>(fp.transitions) - c.fingerprintTransitionsAvg) / c.memberCount;

        assignedClusterId = c.clusterId;
        loggedScore = bestScore;
    }
    else
    {
        Cluster c;
        c.clusterId = m_nextClusterId++;
        c.lastDetectTimeS = burstDetectTimeS;
        c.estimatedPeriodS = -1.0;
        c.memberCount = 1;
        c.relaysSeen = relays;
        c.refFlowKey = hop0FlowKey;
        c.refWindowStart = t0;
        c.refWindowEnd = t1;
        c.fingerprintOnDurationAvg = fp.meanOnDurationS;
        c.fingerprintTransitionsAvg = fp.transitions;
        m_clusters.push_back(c);

        assignedClusterId = c.clusterId;
        loggedScore = -1.0; // marks "founded a new cluster"
    }

    m_out << "{\"trace_id\":" << traceId << ",\"detect_time_s\":" << burstDetectTimeS
          << ",\"assigned_cluster_id\":" << assignedClusterId << ",\"score\":" << loggedScore << ",\"chain\":[";
    for (size_t i = 0; i < chain.size(); ++i)
    {
        m_out << chain[i];
        if (i + 1 < chain.size())
        {
            m_out << ",";
        }
    }
    m_out << "]}" << std::endl;
}

} // namespace livetrace
} // namespace ns3
