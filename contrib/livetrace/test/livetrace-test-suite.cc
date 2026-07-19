#include "ns3/attacker-campaign.h"
#include "ns3/ipv4-address-generator.h"
#include "ns3/livetrace-config.h"
#include "ns3/node-address-index.h"
#include "ns3/observation-log.h"
#include "ns3/random-mesh-topology.h"
#include "ns3/reidentification-engine.h"
#include "ns3/simulator.h"
#include "ns3/test.h"
#include "ns3/timing-correlator.h"
#include "ns3/traceback-observer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <queue>

using namespace ns3;
using namespace ns3::livetrace;

namespace
{

bool
IsConnected(const RandomMeshTopology::BuildResult& r)
{
    uint32_t n = r.nodes.GetN();
    if (n == 0)
    {
        return false;
    }
    std::map<uint32_t, std::vector<uint32_t>> adj;
    for (const auto& e : r.edges)
    {
        adj[e.first].push_back(e.second);
        adj[e.second].push_back(e.first);
    }
    std::vector<bool> visited(n, false);
    std::queue<uint32_t> q;
    q.push(0);
    visited[0] = true;
    uint32_t count = 0;
    while (!q.empty())
    {
        uint32_t u = q.front();
        q.pop();
        count++;
        for (uint32_t v : adj[u])
        {
            if (!visited[v])
            {
                visited[v] = true;
                q.push(v);
            }
        }
    }
    return count == n;
}

} // namespace

class TopologyConnectivityTestCase : public TestCase
{
  public:
    TopologyConnectivityTestCase()
        : TestCase("RandomMeshTopology produces a connected, bottleneck-free mesh")
    {
    }

  private:
    void DoRun() override
    {
        // ns-3's Ipv4AddressGenerator is a process-global singleton that
        // persists across test cases run in the same test-runner binary;
        // reset it so this test's address assignment doesn't collide with
        // (or depend on the order of) any other test case that also builds
        // a topology.
        Ipv4AddressGenerator::Reset();
        RandomMeshTopology topo(30, 0.15, 50, 42);
        RandomMeshTopology::BuildResult result = topo.Build();
        NS_TEST_ASSERT_MSG_GT(result.nodes.GetN(), 0u, "topology should produce at least one node");
        NS_TEST_ASSERT_MSG_GT(result.edges.size(), 0u, "topology should produce at least one edge");
        NS_TEST_ASSERT_MSG_EQ(IsConnected(result), true, "topology must be connected");
    }
};

class ConfigLoaderTestCase : public TestCase
{
  public:
    ConfigLoaderTestCase()
        : TestCase("LiveTraceConfig parses flat and one-level-nested YAML scalars")
    {
    }

  private:
    void DoRun() override
    {
        std::string path = "livetrace-test-config.yaml";
        {
            std::ofstream out(path);
            out << "seed: 7\n";
            out << "topology:\n";
            out << "  n: 64\n";
            out << "  p: 0.2\n";
            out << "sweep:\n";
            out << "  n_values: [10, 20, 30]\n";
        }
        LiveTraceConfig cfg;
        bool ok = cfg.Load(path);
        NS_TEST_ASSERT_MSG_EQ(ok, true, "config file should load");
        NS_TEST_ASSERT_MSG_EQ(cfg.GetInt("seed", -1), 7, "flat scalar seed");
        NS_TEST_ASSERT_MSG_EQ(cfg.GetInt("topology.n", -1), 64, "nested scalar topology.n");
        NS_TEST_ASSERT_MSG_EQ_TOL(cfg.GetDouble("topology.p", -1.0), 0.2, 1e-9, "nested scalar topology.p");
        auto list = cfg.GetIntList("sweep.n_values");
        NS_TEST_ASSERT_MSG_EQ(list.size(), 3u, "n_values list length");
        NS_TEST_ASSERT_MSG_EQ(list[1], 20, "n_values second element");
        std::remove(path.c_str());
    }
};

class ObservationLogTestCase : public TestCase
{
  public:
    ObservationLogTestCase()
        : TestCase("ObservationLog windowed queries by node and by flow")
    {
    }

  private:
    void DoRun() override
    {
        ObservationLog log;
        log.RecordSend(0, "flowA", 1.0, 100);
        log.RecordRecv(1, "flowA", 1.02, 100);
        log.RecordSend(1, "flowB", 1.05, 100);
        log.RecordRecv(2, "flowB", 1.07, 100);

        auto atNode1 = log.EventsAtNode(1, 0.0, 5.0);
        NS_TEST_ASSERT_MSG_EQ(atNode1.size(), 2u, "node 1 should see 2 events (recv flowA, send flowB)");

        auto flowAEvents = log.EventsForFlow("flowA", 0.0, 5.0);
        NS_TEST_ASSERT_MSG_EQ(flowAEvents.size(), 2u, "flowA should have send+recv events");

        auto windowed = log.EventsForFlow("flowA", 0.0, 1.01);
        NS_TEST_ASSERT_MSG_EQ(windowed.size(), 1u, "time window should exclude the later recv");
    }
};

class TimingCorrelatorTestCase : public TestCase
{
  public:
    TimingCorrelatorTestCase()
        : TestCase("TimingCorrelator finds the true upstream match over background noise")
    {
    }

  private:
    void DoRun() override
    {
        ObservationLog log;

        // Reference flow: the confirmed downstream flow (relay -> victim),
        // an on/off pattern of two ~0.3s bursts separated by a ~0.4s gap.
        std::vector<double> onIntervals = {0.0, 0.3, 0.7, 1.0};
        auto emitBurstPattern = [&](const std::string& flowKey, uint32_t txNode, uint32_t rxNode, double jitter) {
            for (size_t seg = 0; seg + 1 < onIntervals.size(); seg += 2)
            {
                for (double t = onIntervals[seg]; t < onIntervals[seg + 1]; t += 0.02)
                {
                    log.RecordSend(txNode, flowKey, t, 512);
                    log.RecordRecv(rxNode, flowKey, t + jitter, 512);
                }
            }
        };

        emitBurstPattern("ref_flow", 100, 1, 0.005);      // reference: relay(1) <- ... already confirmed
        emitBurstPattern("true_upstream", 200, 1, 0.006); // true match: same on/off shape, tiny extra jitter

        // Noise flow at the same candidate node: unrelated, roughly-constant
        // low-rate chatter with no matching on/off structure.
        for (double t = 0.0; t < 1.0; t += 0.05)
        {
            log.RecordSend(300, "noise_flow", t, 512);
            log.RecordRecv(1, "noise_flow", t + 0.005, 512);
        }

        TimingCorrelator::Config ccfg;
        ccfg.bucketS = 0.05;
        ccfg.onThresholdPps = 1.0;
        ccfg.minOnOffTransitions = 2;
        ccfg.maxLagBuckets = 2;
        TimingCorrelator correlator(&log, ccfg);

        auto match = correlator.FindBestUpstreamMatch("ref_flow", /*candidateNodeId=*/1, 0.0, 1.0,
                                                        /*scoreThreshold=*/0.5, {"ref_flow"});
        NS_TEST_ASSERT_MSG_EQ(match.valid, true, "true upstream match should score above threshold");
        NS_TEST_ASSERT_MSG_EQ(match.flowKey, "true_upstream", "correlator should pick the true match, not noise");

        double noiseScore = correlator.Correlate(correlator.ComputeOnOffSignal("ref_flow", 0.0, 1.0),
                                                   correlator.ComputeOnOffSignal("noise_flow", 0.0, 1.0));
        NS_TEST_ASSERT_MSG_GT(match.score, noiseScore, "true match must score higher than background noise");
    }
};

class ReidentificationEngineTestCase : public TestCase
{
  public:
    ReidentificationEngineTestCase()
        : TestCase("ReidentificationEngine separates two periodic actors by periodicity+convergence+timing")
    {
    }

  private:
    static void EmitPattern(ObservationLog& log, const std::string& flowKey, double t0)
    {
        for (int seg = 0; seg < 3; ++seg)
        {
            double segStart = t0 + seg * 0.3;
            for (double t = segStart; t < segStart + 0.1; t += 0.02)
            {
                log.RecordSend(100, flowKey, t, 512);
                log.RecordRecv(0, flowKey, t + 0.005, 512);
            }
        }
    }

    void DoRun() override
    {
        ObservationLog log;
        TimingCorrelator::Config corrCfg;
        corrCfg.bucketS = 0.05;
        corrCfg.onThresholdPps = 1.0;
        corrCfg.minOnOffTransitions = 2;
        corrCfg.maxLagBuckets = 2;
        TimingCorrelator correlator(&log, corrCfg);

        ReidentificationEngine::Config reidCfg;
        reidCfg.periodPriorS = 40.0;
        reidCfg.periodicityToleranceS = 3.0;
        reidCfg.evidenceWindowS = 1.0;
        reidCfg.periodicityWeight = 0.40;
        reidCfg.timingWeight = 0.45;
        reidCfg.fingerprintWeight = 0.15;
        reidCfg.clusterScoreThreshold = 0.5;

        std::string outPath = "livetrace-test-reid.jsonl";
        {
            ReidentificationEngine engine(&log, &correlator, reidCfg, outPath);

            // Actor A: period 40s starting at t=0, relays {1,2}.
            // Actor B: period 40s starting at t=5 (different phase), relays {3,4}.
            struct Burst
            {
                uint32_t traceId;
                double t;
                std::string flowKey;
                std::vector<uint32_t> chain;
            };
            std::vector<Burst> bursts = {
                {0, 0.0, "A0", {0, 1, 2, 1000}},   {1, 5.0, "B5", {0, 3, 4, 2000}},
                {2, 40.0, "A40", {0, 1, 2, 1001}}, {3, 45.0, "B45", {0, 3, 4, 2001}},
                {4, 80.0, "A80", {0, 1, 2, 1002}}, {5, 85.0, "B85", {0, 3, 4, 2002}},
            };
            for (const auto& b : bursts)
            {
                EmitPattern(log, b.flowKey, b.t);
                engine.OnTraceComplete(b.traceId, b.t, b.chain, "no_match_above_threshold", b.flowKey);
            }
        } // engine destructor flushes/closes outPath

        std::ifstream in(outPath);
        std::map<uint32_t, int> traceToCluster;
        std::string line;
        while (std::getline(in, line))
        {
            size_t tp = line.find("\"trace_id\":");
            size_t cp = line.find("\"assigned_cluster_id\":");
            NS_TEST_ASSERT_MSG_EQ(tp != std::string::npos && cp != std::string::npos, true,
                                  "reid output line must contain trace_id and assigned_cluster_id");
            uint32_t traceId = static_cast<uint32_t>(std::atoi(line.c_str() + tp + strlen("\"trace_id\":")));
            int clusterId = std::atoi(line.c_str() + cp + strlen("\"assigned_cluster_id\":"));
            traceToCluster[traceId] = clusterId;
        }
        in.close();
        std::remove(outPath.c_str());

        NS_TEST_ASSERT_MSG_EQ(traceToCluster.size(), 6u, "expected one reid record per burst");
        NS_TEST_ASSERT_MSG_EQ(traceToCluster[0], traceToCluster[2], "actor A's bursts should share a cluster");
        NS_TEST_ASSERT_MSG_EQ(traceToCluster[2], traceToCluster[4], "actor A's bursts should share a cluster");
        NS_TEST_ASSERT_MSG_EQ(traceToCluster[1], traceToCluster[3], "actor B's bursts should share a cluster");
        NS_TEST_ASSERT_MSG_EQ(traceToCluster[3], traceToCluster[5], "actor B's bursts should share a cluster");
        NS_TEST_ASSERT_MSG_NE(traceToCluster[0], traceToCluster[1], "actor A and B must not share a cluster");
    }
};

namespace
{

// Two ~0.3s ON segments separated by a ~0.4s gap, starting at t0 -- the same
// shape as TimingCorrelatorTestCase's pattern, factored out here so the
// TracebackObserver tests below can build a multi-hop chain of correlated
// flows without duplicating this per test case.
void
EmitTracebackBurst(ObservationLog& log,
                    const std::string& flowKey,
                    uint32_t txNode,
                    uint32_t rxNode,
                    double t0,
                    double jitter)
{
    static const std::vector<double> onIntervals = {0.0, 0.3, 0.7, 1.0};
    for (size_t seg = 0; seg + 1 < onIntervals.size(); seg += 2)
    {
        for (double t = onIntervals[seg]; t < onIntervals[seg + 1]; t += 0.02)
        {
            log.RecordSend(txNode, flowKey, t0 + t, 512);
            log.RecordRecv(rxNode, flowKey, t0 + t + jitter, 512);
        }
    }
}

struct TracebackRunResult
{
    std::vector<uint32_t> chain;
    std::string stopReason;
    uint32_t hopConfirmedCount;
};

/// Builds a synthetic 3-node victim(1) <- relay(2) <- origin(3) observation
/// log with a correlated on/off pattern on both hops, drives a real
/// TracebackObserver against it under the given config (starting from a
/// simulated flow arrival at the victim), and returns the resulting chain,
/// stop reason, and hop-confirmed count. Node ids and addresses are fixed
/// across all callers. Advances/resets the ns-3 Simulator internally, so
/// callers don't need to manage Simulator state themselves.
TracebackRunResult
RunTracebackScenario(const TracebackObserver::Config& obsCfg)
{
    constexpr uint32_t kVictim = 1;
    constexpr uint32_t kRelay = 2;
    constexpr uint32_t kOrigin = 3;

    ObservationLog log;
    NodeAddressIndex addrIndex;
    addrIndex.Register(Ipv4Address("10.0.0.1"), kVictim);
    addrIndex.Register(Ipv4Address("10.0.0.2"), kRelay);
    addrIndex.Register(Ipv4Address("10.0.0.3"), kOrigin);

    std::string hop0FlowKey = "10.0.0.2:5000->10.0.0.1:9999";     // relay -> victim
    std::string upstreamFlowKey = "10.0.0.3:6000->10.0.0.2:5000"; // origin -> relay

    // Anchor all timestamps to whatever Simulator::Now() currently is
    // (rather than an absolute constant) so this helper is safe to call more
    // than once per test case / across test cases sharing a process.
    double t0 = Simulator::Now().GetSeconds();
    double detectTime = t0 + 10.0;
    EmitTracebackBurst(log, hop0FlowKey, kRelay, kVictim, detectTime, 0.005);
    EmitTracebackBurst(log, upstreamFlowKey, kOrigin, kRelay, detectTime, 0.006);

    TimingCorrelator::Config corrCfg;
    corrCfg.bucketS = 0.05;
    corrCfg.onThresholdPps = 1.0;
    corrCfg.minOnOffTransitions = 2;
    corrCfg.maxLagBuckets = 2;
    TimingCorrelator correlator(&log, corrCfg);

    TracebackRunResult result;
    result.hopConfirmedCount = 0;
    {
        TracebackObserver observer(&log, &addrIndex, &correlator, kVictim, obsCfg,
                                    "livetrace-test-traceback-scratch.jsonl");
        observer.SetHopConfirmedNotify(
            [&result](uint32_t, uint32_t, uint32_t, double, std::string) { result.hopConfirmedCount++; });
        observer.SetTraceCompleteNotify([&result](uint32_t,
                                                    double,
                                                    std::vector<uint32_t> chain,
                                                    std::string stopReason,
                                                    std::string,
                                                    std::string) {
            result.chain = chain;
            result.stopReason = stopReason;
        });
        Simulator::Schedule(Seconds(detectTime - t0),
                             &TracebackObserver::OnFlowObserved,
                             &observer,
                             kVictim,
                             hop0FlowKey,
                             detectTime,
                             Ipv4Address("10.0.0.2"));
        Simulator::Run();
    }
    Simulator::Destroy();
    std::remove("livetrace-test-traceback-scratch.jsonl");
    return result;
}

} // namespace

class TracebackObserverTestCase : public TestCase
{
  public:
    TracebackObserverTestCase()
        : TestCase("TracebackObserver resolves a 2-hop stepping-stone chain to its true origin via timing correlation")
    {
    }

  private:
    void DoRun() override
    {
        TracebackObserver::Config obsCfg;
        obsCfg.liveWindowS = 5.0;
        obsCfg.accumulationDelayS = 2.0;
        obsCfg.hopDelayS = 0.6;
        obsCfg.maxHops = 10;
        obsCfg.scoreThreshold = 0.5;

        TracebackRunResult result = RunTracebackScenario(obsCfg);

        std::vector<uint32_t> expected = {1, 2, 3}; // victim, relay, origin
        NS_TEST_ASSERT_MSG_EQ(result.chain.size(), expected.size(), "chain should resolve victim, relay, origin");
        for (size_t i = 0; i < expected.size() && i < result.chain.size(); ++i)
        {
            NS_TEST_ASSERT_MSG_EQ(result.chain[i], expected[i], "chain element mismatch");
        }
        NS_TEST_ASSERT_MSG_EQ(result.stopReason,
                              "no_match_above_threshold",
                              "walk should stop at the origin, which has no further upstream flow to find");
        NS_TEST_ASSERT_MSG_EQ(result.hopConfirmedCount, 1u, "exactly one hop (the relay) should be confirmed");
    }
};

class TracebackObserverStopReasonsTestCase : public TestCase
{
  public:
    TracebackObserverStopReasonsTestCase()
        : TestCase("TracebackObserver reports window_expired and hop_limit_reached at the right point in the walk")
    {
    }

  private:
    void DoRun() override
    {
        {
            // liveWindowS shorter than accumulationDelayS: the very first
            // AttemptTrace call already runs past the deadline, before any
            // correlation is attempted.
            TracebackObserver::Config obsCfg;
            obsCfg.liveWindowS = 0.5;
            obsCfg.accumulationDelayS = 2.0;
            obsCfg.hopDelayS = 0.6;
            obsCfg.maxHops = 10;
            obsCfg.scoreThreshold = 0.5;

            TracebackRunResult result = RunTracebackScenario(obsCfg);
            std::vector<uint32_t> expected = {1, 2}; // victim, relay -- resolved but not yet correlated
            NS_TEST_ASSERT_MSG_EQ(result.chain.size(), expected.size(), "window_expired chain length");
            for (size_t i = 0; i < expected.size() && i < result.chain.size(); ++i)
            {
                NS_TEST_ASSERT_MSG_EQ(result.chain[i], expected[i], "window_expired chain element mismatch");
            }
            NS_TEST_ASSERT_MSG_EQ(result.stopReason,
                                  "window_expired",
                                  "a live window shorter than the accumulation delay must expire before hop 0");
        }
        {
            // maxHops=1: the relay hop is confirmed (a real upstream match
            // exists), but the walk must stop before following it.
            TracebackObserver::Config obsCfg;
            obsCfg.liveWindowS = 5.0;
            obsCfg.accumulationDelayS = 2.0;
            obsCfg.hopDelayS = 0.6;
            obsCfg.maxHops = 1;
            obsCfg.scoreThreshold = 0.5;

            TracebackRunResult result = RunTracebackScenario(obsCfg);
            std::vector<uint32_t> expected = {1, 2, 3};
            NS_TEST_ASSERT_MSG_EQ(result.chain.size(), expected.size(), "hop_limit_reached chain length");
            for (size_t i = 0; i < expected.size() && i < result.chain.size(); ++i)
            {
                NS_TEST_ASSERT_MSG_EQ(result.chain[i], expected[i], "hop_limit_reached chain element mismatch");
            }
            NS_TEST_ASSERT_MSG_EQ(result.stopReason,
                                  "hop_limit_reached",
                                  "maxHops must cut the walk off even though a real upstream match exists");
            NS_TEST_ASSERT_MSG_EQ(result.hopConfirmedCount, 1u, "the relay hop still gets confirmed before the cutoff");
        }
    }
};

class TracebackObserverCycleDetectedTestCase : public TestCase
{
  public:
    TracebackObserverCycleDetectedTestCase()
        : TestCase(
              "TracebackObserver stops with cycle_detected rather than looping when the best match points back "
              "into the chain")
    {
    }

  private:
    void DoRun() override
    {
        constexpr uint32_t kVictim = 1;
        constexpr uint32_t kRelay = 2;

        ObservationLog log;
        NodeAddressIndex addrIndex;
        addrIndex.Register(Ipv4Address("10.0.0.1"), kVictim);
        addrIndex.Register(Ipv4Address("10.0.0.2"), kRelay);

        std::string hop0FlowKey = "10.0.0.2:5000->10.0.0.1:9999"; // relay -> victim
        // A contrived "upstream" flow whose source is the VICTIM itself --
        // the only candidate inbound flow at the relay, so the correlator is
        // forced to match onto it, sending the walk back into a node already
        // in the chain.
        std::string loopFlowKey = "10.0.0.1:6000->10.0.0.2:5000"; // victim -> relay

        double t0 = Simulator::Now().GetSeconds();
        double detectTime = t0 + 10.0;
        EmitTracebackBurst(log, hop0FlowKey, kRelay, kVictim, detectTime, 0.005);
        EmitTracebackBurst(log, loopFlowKey, kVictim, kRelay, detectTime, 0.006);

        TimingCorrelator::Config corrCfg;
        corrCfg.bucketS = 0.05;
        corrCfg.onThresholdPps = 1.0;
        corrCfg.minOnOffTransitions = 2;
        corrCfg.maxLagBuckets = 2;
        TimingCorrelator correlator(&log, corrCfg);

        TracebackObserver::Config obsCfg;
        obsCfg.liveWindowS = 5.0;
        obsCfg.accumulationDelayS = 2.0;
        obsCfg.hopDelayS = 0.6;
        obsCfg.maxHops = 10;
        obsCfg.scoreThreshold = 0.5;

        std::vector<uint32_t> finalChain;
        std::string finalStopReason;
        {
            TracebackObserver observer(&log, &addrIndex, &correlator, kVictim, obsCfg,
                                        "livetrace-test-traceback-cycle.jsonl");
            observer.SetTraceCompleteNotify([&finalChain, &finalStopReason](uint32_t,
                                                                              double,
                                                                              std::vector<uint32_t> chain,
                                                                              std::string stopReason,
                                                                              std::string,
                                                                              std::string) {
                finalChain = chain;
                finalStopReason = stopReason;
            });
            Simulator::Schedule(Seconds(detectTime - t0),
                                 &TracebackObserver::OnFlowObserved,
                                 &observer,
                                 kVictim,
                                 hop0FlowKey,
                                 detectTime,
                                 Ipv4Address("10.0.0.2"));
            Simulator::Run();
        }
        Simulator::Destroy();
        std::remove("livetrace-test-traceback-cycle.jsonl");

        std::vector<uint32_t> expected = {kVictim, kRelay, kVictim};
        NS_TEST_ASSERT_MSG_EQ(finalChain.size(), expected.size(), "cycle chain should be victim, relay, victim");
        for (size_t i = 0; i < expected.size() && i < finalChain.size(); ++i)
        {
            NS_TEST_ASSERT_MSG_EQ(finalChain[i], expected[i], "cycle chain element mismatch");
        }
        NS_TEST_ASSERT_MSG_EQ(finalStopReason,
                              "cycle_detected",
                              "walk must stop rather than loop when the best match points back into the chain");
    }
};

class CorrelatorRejectsNoiseAndFlatTestCase : public TestCase
{
  public:
    CorrelatorRejectsNoiseAndFlatTestCase()
        : TestCase("TimingCorrelator rejects out-of-phase bursts and flat (non-bursty) flows")
    {
    }

  private:
    void DoRun() override
    {
        ObservationLog log;
        TimingCorrelator::Config ccfg;
        ccfg.bucketS = 0.05;
        ccfg.onThresholdPps = 1.0;
        ccfg.minOnOffTransitions = 2;
        ccfg.maxLagBuckets = 2; // +/- 0.1s lag search window

        TimingCorrelator correlator(&log, ccfg);

        // Two flows with the SAME on/off shape but shifted 1.5s apart --
        // far beyond the +/-0.1s lag search window, so despite each having a
        // legitimate bursty pattern individually, they must not score as a
        // match against each other.
        EmitTracebackBurst(log, "flowA", 100, 1, 0.0, 0.005);
        EmitTracebackBurst(log, "flowB", 200, 1, 1.5, 0.005);
        auto sigA = correlator.ComputeOnOffSignal("flowA", 0.0, 3.0);
        auto sigB = correlator.ComputeOnOffSignal("flowB", 0.0, 3.0);
        double outOfPhaseScore = correlator.Correlate(sigA, sigB);
        NS_TEST_ASSERT_MSG_EQ(outOfPhaseScore < 0.5,
                              true,
                              "out-of-phase bursts must not score above the observer's default match threshold");

        // A flow with a constant, non-bursty rate never crosses enough
        // on/off transitions to be trusted -- Correlate must return the -1
        // sentinel rather than a spurious real score.
        for (double t = 0.0; t < 2.0; t += 0.05)
        {
            log.RecordSend(300, "flat_flow", t, 512);
            log.RecordRecv(1, "flat_flow", t + 0.005, 512);
        }
        auto sigFlat = correlator.ComputeOnOffSignal("flat_flow", 0.0, 2.0);
        double flatScore = correlator.Correlate(sigA, sigFlat);
        NS_TEST_ASSERT_MSG_EQ_TOL(flatScore,
                                  -1.0,
                                  1e-9,
                                  "a flow below minOnOffTransitions must return the -1 sentinel, not a real score");
    }
};

class TopologyDiameterGrowsWithFixedAvgDegreeTestCase : public TestCase
{
  public:
    TopologyDiameterGrowsWithFixedAvgDegreeTestCase()
        : TestCase("Fixed avg-degree topology has a non-trivial (>2), non-flat diameter at moderate N")
    {
    }

  private:
    void DoRun() override
    {
        // Phase 6 fix: with a fixed edge probability p, denser graphs at
        // large N kept diameter ~flat (2-3 hops even at N=320), decoupling
        // traceback difficulty from N entirely. Fixing mean degree instead
        // (p = avgDegree/(n-1)) lets diameter grow (slowly, ~log N) with N;
        // this is the load-bearing assumption behind tying attacker chain
        // length to diameter. Only one topology is built here (rather than
        // comparing two N values in-process) because ns-3's
        // Ipv4AddressGenerator is a process-global singleton that collides
        // on a second Build() call in the same test binary -- real N-scaling
        // evidence (diameter 3/4/5/6 at N=20/80/320/640, all with
        // avg_degree=8) was gathered from separate scratch/livetrace-sim
        // process invocations and is recorded in docs/summaries/phase6.
        Ipv4AddressGenerator::Reset();
        double avgDegree = 8.0;
        uint32_t n = 200;
        double p = avgDegree / static_cast<double>(n - 1);
        RandomMeshTopology topo(n, p, 50, 7);
        RandomMeshTopology::BuildResult result = topo.Build();

        NS_TEST_ASSERT_MSG_GT(result.diameter, 2u,
                              "diameter at N=200, avg_degree=8 should exceed the ~2-hop diameter "
                              "the old fixed-p=0.12 generator produced at large N");
    }
};

class ChainLengthTargetLawTestCase : public TestCase
{
  public:
    ChainLengthTargetLawTestCase()
        : TestCase("AttackerCampaign::ComputeChainLengthTarget follows factor*diameter, floored and capped")
    {
    }

  private:
    void DoRun() override
    {
        // target = round(factor * diameter), floored at chainLenMin, capped
        // at chainLenAbsoluteCap. Pure function, no RNG -- exact values.
        NS_TEST_ASSERT_MSG_EQ(AttackerCampaign::ComputeChainLengthTarget(2, 40, 1.2, 3), 4u,
                              "round(1.2*3)=4 (round-half-away-from-zero of 3.6)");
        NS_TEST_ASSERT_MSG_EQ(AttackerCampaign::ComputeChainLengthTarget(2, 40, 1.2, 5), 6u, "round(1.2*5)=6");
        NS_TEST_ASSERT_MSG_EQ(AttackerCampaign::ComputeChainLengthTarget(2, 40, 1.2, 6), 7u, "round(1.2*6)=7.2->7");
        // Floor: a diameter of 0 or 1 must not push the target below chainLenMin.
        NS_TEST_ASSERT_MSG_EQ(AttackerCampaign::ComputeChainLengthTarget(2, 40, 1.2, 0), 2u,
                              "floored at chainLenMin even for a trivial diameter");
        // Cap: a huge diameter must not exceed the absolute safety ceiling.
        NS_TEST_ASSERT_MSG_EQ(AttackerCampaign::ComputeChainLengthTarget(2, 10, 1.2, 100), 10u,
                              "capped at chainLenAbsoluteCap regardless of diameter");
        // Monotonic in diameter (the core scaling property the sweep relies on).
        uint32_t prev = 0;
        for (uint32_t diam = 1; diam <= 10; ++diam)
        {
            uint32_t target = AttackerCampaign::ComputeChainLengthTarget(2, 40, 1.2, diam);
            NS_TEST_ASSERT_MSG_EQ(target >= prev, true, "chain length target must be non-decreasing in diameter");
            prev = target;
        }
    }
};

class LiveTraceTestSuite : public TestSuite
{
  public:
    LiveTraceTestSuite()
        : TestSuite("livetrace", Type::UNIT)
    {
        AddTestCase(new TopologyConnectivityTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new ConfigLoaderTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new ObservationLogTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new TimingCorrelatorTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new ReidentificationEngineTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new TracebackObserverTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new TracebackObserverStopReasonsTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new TracebackObserverCycleDetectedTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new CorrelatorRejectsNoiseAndFlatTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new TopologyDiameterGrowsWithFixedAvgDegreeTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new ChainLengthTargetLawTestCase(), TestCase::Duration::QUICK);
    }
};

static LiveTraceTestSuite g_liveTraceTestSuite;
