#ifndef LIVETRACE_REIDENTIFICATION_ENGINE_H
#define LIVETRACE_REIDENTIFICATION_ENGINE_H

#include "observation-log.h"
#include "timing-correlator.h"

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
 * Cross-burst re-identification: as each TracebackObserver trace completes,
 * decide which persistent "actor" cluster it belongs to using only what is
 * observable about the completed trace -- never OracleLogger. Combines:
 *   - path convergence + periodicity (primary): does the burst's timing fit
 *     the cluster's estimated period, and does its relay chain overlap the
 *     cluster's previously-seen relay nodes?
 *   - timing correlation (workhorse): on/off signal similarity between this
 *     burst's victim-inbound flow and the cluster's reference burst, reusing
 *     the same Zhang-Paxson-style correlator as the hop-by-hop traceback.
 *   - behavioral fingerprint (auxiliary): mean on-duration and on/off
 *     transition count of the burst's signal.
 * A burst that doesn't score above threshold against any known cluster
 * founds a new one -- the engine is never told how many actors exist.
 */
class ReidentificationEngine
{
  public:
    struct Config
    {
        double periodPriorS;         // nominal period, used before a cluster has its own estimate
        double periodicityToleranceS;
        double evidenceWindowS;      // how much of each burst's signal to use for scoring
        double periodicityWeight;
        double timingWeight;
        double fingerprintWeight;
        double clusterScoreThreshold;
    };

    ReidentificationEngine(ObservationLog* obsLog, TimingCorrelator* correlator, Config cfg, const std::string& outPath);

    /// Hook this to TracebackObserver::SetTraceCompleteNotify.
    void OnTraceComplete(uint32_t traceId,
                         double burstDetectTimeS,
                         std::vector<uint32_t> chain,
                         std::string stopReason,
                         std::string hop0FlowKey);

  private:
    struct Fingerprint
    {
        double meanOnDurationS;
        uint32_t transitions;
    };

    struct Cluster
    {
        uint32_t clusterId;
        double lastDetectTimeS;
        double estimatedPeriodS; // <= 0 means not yet estimated
        uint32_t memberCount;
        std::set<uint32_t> relaysSeen;
        std::string refFlowKey;
        double refWindowStart;
        double refWindowEnd;
        double fingerprintOnDurationAvg;
        double fingerprintTransitionsAvg;
    };

    Fingerprint ComputeFingerprint(const std::string& flowKey, double t0, double t1) const;
    double ScoreAgainstCluster(const Cluster& c,
                                double detectTimeS,
                                const std::set<uint32_t>& relays,
                                const std::string& hop0FlowKey,
                                double t0,
                                double t1,
                                const Fingerprint& fp) const;

    ObservationLog* m_obsLog;
    TimingCorrelator* m_correlator;
    Config m_cfg;
    std::ofstream m_out;
    std::vector<Cluster> m_clusters;
    uint32_t m_nextClusterId;
};

} // namespace livetrace
} // namespace ns3

#endif // LIVETRACE_REIDENTIFICATION_ENGINE_H
