#ifndef LIVETRACE_TIMING_CORRELATOR_H
#define LIVETRACE_TIMING_CORRELATOR_H

#include "observation-log.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ns3
{
namespace livetrace
{

/**
 * Zhang-Paxson style on/off timing correlation. This is the whole basis for
 * hop-by-hop traceback: a stepping-stone relay's outbound flow tends to
 * reproduce the on/off (burst/idle) pattern of whichever inbound flow it is
 * relaying, with only a small delay. Flow 5-tuples carry no field linking an
 * inbound connection to the outbound one it feeds (they're two unrelated
 * connections at the IP/UDP level) -- timing correlation is what recovers
 * that link from ObservationLog data alone.
 */
class TimingCorrelator
{
  public:
    struct Config
    {
        double bucketS;             // on/off bucketing resolution
        double onThresholdPps;      // packet rate above which a bucket counts as ON
        uint32_t minOnOffTransitions; // minimum ON<->OFF transitions required to trust a score
        int maxLagBuckets;          // search window for small relay-delay misalignment
    };

    struct Match
    {
        std::string flowKey;
        double score;
        bool valid;
    };

    TimingCorrelator(ObservationLog* obsLog, Config cfg);

    /// Binary on/off signal for one flow over [t0, t1], bucketed at bucketS.
    std::vector<int> ComputeOnOffSignal(const std::string& flowKey, double t0, double t1) const;

    /// Best-lag-aligned correlation score in [-1, 1] between two signals
    /// (Pearson correlation at whichever lag in [-maxLagBuckets, maxLagBuckets]
    /// maximizes it). Returns -1 if either signal has fewer than
    /// minOnOffTransitions on/off transitions (too flat to trust).
    double Correlate(const std::vector<int>& a, const std::vector<int>& b) const;

    /// Among all flows observed at candidateNodeId in [t0, t1] that have at
    /// least one RX event there (candidateNodeId is the destination) and are
    /// not in excludeFlowKeys, find the one whose on/off signal best matches
    /// referenceFlowKey's. Returns valid=false if no candidate scores at or
    /// above scoreThreshold.
    Match FindBestUpstreamMatch(const std::string& referenceFlowKey,
                                 uint32_t candidateNodeId,
                                 double t0,
                                 double t1,
                                 double scoreThreshold,
                                 const std::vector<std::string>& excludeFlowKeys) const;

  private:
    uint32_t CountTransitions(const std::vector<int>& signal) const;

    ObservationLog* m_obsLog;
    Config m_cfg;
};

} // namespace livetrace
} // namespace ns3

#endif // LIVETRACE_TIMING_CORRELATOR_H
