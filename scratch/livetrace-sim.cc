// LiveTrace simulation driver.
//
// Builds a random relay mesh, runs one moving-origin periodic attacker
// (stepping-stone chains) plus ordinary background traffic against a fixed
// victim, with ground truth recorded only to the oracle log and observable
// traffic recorded only to the observation log. When correlation.enabled is
// true (default from Phase 1 on), an online TracebackObserver attempts
// hop-by-hop traceback from every new flow arriving at the victim, using
// only ObservationLog + the public node/address map -- never the oracle.

#include "ns3/animation-interface.h"
#include "ns3/attacker-campaign.h"
#include "ns3/background-traffic.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-header.h"
#include "ns3/livetrace-config.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/node-address-index.h"
#include "ns3/observation-log.h"
#include "ns3/oracle-logger.h"
#include "ns3/random-mesh-topology.h"
#include "ns3/reidentification-engine.h"
#include "ns3/stepstone-relay-app.h"
#include "ns3/timing-correlator.h"
#include "ns3/traceback-observer.h"
#include "ns3/udp-header.h"

#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <sstream>

using namespace ns3;
using namespace ns3::livetrace;

NS_LOG_COMPONENT_DEFINE("LiveTraceSim");

namespace
{
constexpr uint16_t kVictimPort = 9999;
constexpr uint16_t kBackgroundPort = 8000;

void
LayoutNodesOnCircle(NodeContainer& nodes, double radius = 500.0)
{
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positions = CreateObject<ListPositionAllocator>();
    uint32_t n = nodes.GetN();
    for (uint32_t i = 0; i < n; ++i)
    {
        double angle = 2.0 * M_PI * static_cast<double>(i) / std::max<uint32_t>(n, 1);
        positions->Add(Vector(radius + radius * std::cos(angle), radius + radius * std::sin(angle), 0.0));
    }
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);
}

/// Reproduces livetrace::MakeFlowKey's "<addr>:<port>-><addr>:<port>" format
/// locally (rather than including contrib/livetrace/model/flow-key-util.h via
/// "ns3/flow-key-util.h") because that header name collides with an
/// unrelated sibling module also symlinked into this ns-3-dev checkout
/// (contrib/junktrace, from a separate project), which wins the flattened
/// ns3/ include namespace and shadows livetrace's own version.
std::string
OverlayFlowKey(Ipv4Address srcAddr, uint16_t srcPort, Ipv4Address dstAddr, uint16_t dstPort)
{
    std::ostringstream oss;
    srcAddr.Print(oss);
    oss << ":" << srcPort << "->";
    dstAddr.Print(oss);
    oss << ":" << dstPort;
    return oss.str();
}

/**
 * Physical (L3) relay lookup for the NetAnim visualization only: records,
 * for every overlay UDP flow, which nodes performed pure IP forwarding
 * (as opposed to originating or terminating it) by hooking Ipv4L3Protocol's
 * UnicastForward trace on every node. This has no bearing on traceback or
 * correlation -- those never look at IP-forwarding events, only at flows
 * observed by StepstoneRelayApp -- it exists purely so NetAnim can show the
 * physical route underneath a confirmed overlay hop (pale-red intermediate
 * nodes) alongside the overlay hop itself (red confirmed relay).
 */
class PhysicalRouteIndex
{
  public:
    void Connect()
    {
        Config::Connect("/NodeList/*/$ns3::Ipv4L3Protocol/UnicastForward",
                         MakeCallback(&PhysicalRouteIndex::OnForward, this));
    }

    const std::set<uint32_t>& RelaysFor(const std::string& flowKey) const
    {
        static const std::set<uint32_t> kEmpty;
        auto it = m_relaysByFlow.find(flowKey);
        return it == m_relaysByFlow.end() ? kEmpty : it->second;
    }

  private:
    void OnForward(std::string context, const Ipv4Header& header, Ptr<const Packet> packet, uint32_t /*interface*/)
    {
        UdpHeader udpHdr;
        if (packet->PeekHeader(udpHdr) == 0)
        {
            return; // not UDP -- this simulation only ever sends UDP
        }
        if (udpHdr.GetDestinationPort() == kBackgroundPort)
        {
            // Background noise flows are never looked up by the visualizer;
            // skipping them keeps this map small on the full N-sweep runs
            // (where background traffic is by far the largest packet volume).
            return;
        }
        std::string flowKey = OverlayFlowKey(header.GetSource(), udpHdr.GetSourcePort(), header.GetDestination(),
                                              udpHdr.GetDestinationPort());
        m_relaysByFlow[flowKey].insert(ParseNodeId(context));
    }

    static uint32_t ParseNodeId(const std::string& context)
    {
        const std::string prefix = "/NodeList/";
        size_t start = context.find(prefix) + prefix.size();
        size_t end = context.find('/', start);
        return static_cast<uint32_t>(std::stoul(context.substr(start, end - start)));
    }

    std::map<std::string, std::set<uint32_t>> m_relaysByFlow;
};

/**
 * Drives NetAnim node coloring from the traceback observer's live callbacks.
 * Colors form a priority ladder:
 *   green (victim) > purple (this burst's estimated origin) > red (confirmed
 *   L7 relay hop) > pale red (L3-only physical forwarder under a confirmed
 *   hop) > light purple (the immediately preceding burst's estimated origin)
 *   > grey (idle).
 * Only a single node -- the origin estimated in the immediately preceding
 * burst -- carries the light-purple marker at any time; when a newer origin
 * takes over, the previous holder simply goes back to grey (origins older
 * than the immediately preceding one are not kept). That marker sits *below*
 * red/pale-red, so if the current burst's chain crosses it, it lights up
 * red/pale-red for that burst like any other node. "Reset", fired at the
 * start of each new trace, clears the *previous* trace's red / pale-red
 * highlighting -- an active light-purple holder stays light purple, the
 * newly finished origin becomes the light-purple holder (demoting purple
 * "latest origin" in the same step), and everything else goes back to grey.
 * This assumes traces don't overlap in time, which holds for the
 * single-actor visualization config this is intended for; it does not
 * attempt to disentangle concurrently in-flight traces from multiple
 * simultaneous actors.
 */
class TraceVisualizer
{
  public:
    TraceVisualizer(AnimationInterface& anim, PhysicalRouteIndex& routes, uint32_t victimNodeId, double nodeSize)
        : m_anim(anim),
          m_routes(routes),
          m_victimNodeId(victimNodeId),
          m_nodeSize(nodeSize)
    {
    }

    void OnTraceStarted(uint32_t /*traceId*/, double /*timeS*/)
    {
        for (uint32_t nodeId : m_pendingReset)
        {
            ResetNode(nodeId);
        }
        m_pendingReset.clear();
    }

    void OnHopConfirmed(uint32_t /*traceId*/,
                         uint32_t nodeId,
                         uint32_t /*hopsSoFar*/,
                         double /*evalTimeS*/,
                         const std::string& downstreamFlowKey)
    {
        Paint(nodeId, VizColor::kRed);
        m_pendingReset.insert(nodeId);
        PaintPhysicalRelays(downstreamFlowKey);
    }

    void OnTraceComplete(uint32_t /*traceId*/,
                          const std::vector<uint32_t>& chain,
                          const std::string& stopReason,
                          const std::string& lastConfirmedFlowKey)
    {
        // Only "no_match_above_threshold" means the walk actually ran out of
        // upstream flow at chain.back() -- i.e. it reached the true origin
        // (or lost the trail right at that node). Other stop reasons
        // (window_expired, hop_limit_reached, ...) are a live-window budget
        // running out mid-chain, not an origin estimate, so they get no
        // special color.
        if (stopReason == "no_match_above_threshold" && !chain.empty())
        {
            uint32_t originGuess = chain.back();
            if (originGuess != m_victimNodeId)
            {
                Paint(originGuess, VizColor::kLatestOrigin);
                m_pendingReset.insert(originGuess);
                PaintPhysicalRelays(lastConfirmedFlowKey);
            }
        }
    }

  private:
    enum class VizColor
    {
        kGrey = 0,
        kPastOrigin = 1, // low-priority marker: the immediately preceding burst's estimated origin
        kPaleRed = 2,
        kRed = 3,
        kLatestOrigin = 4,
    };

    void PaintPhysicalRelays(const std::string& flowKey)
    {
        for (uint32_t relayNode : m_routes.RelaysFor(flowKey))
        {
            Paint(relayNode, VizColor::kPaleRed);
            m_pendingReset.insert(relayNode);
        }
    }

    // Upgrade-only: never lets a lower-priority color overwrite a
    // higher-priority one already showing (e.g. pale red can't dim a red
    // node, but red CAN paint over the light-purple past-origin marker --
    // that low rank is what lets the current burst's path show through it).
    void Paint(uint32_t nodeId, VizColor color)
    {
        if (nodeId == m_victimNodeId || RankOf(color) <= RankOf(m_state[nodeId]))
        {
            return;
        }
        Apply(nodeId, color);
    }

    void ResetNode(uint32_t nodeId)
    {
        if (nodeId == m_victimNodeId)
        {
            return;
        }
        switch (m_state[nodeId])
        {
        case VizColor::kLatestOrigin:
            // This burst's guess becomes the sole light-purple "immediately
            // preceding origin" marker; whoever held it before loses it (back
            // to grey -- older origins are not kept).
            if (m_mostRecentPastOrigin.has_value() && *m_mostRecentPastOrigin != nodeId)
            {
                Apply(*m_mostRecentPastOrigin, VizColor::kGrey);
            }
            m_mostRecentPastOrigin = nodeId;
            Apply(nodeId, VizColor::kPastOrigin);
            break;
        case VizColor::kRed:
        case VizColor::kPaleRed:
            // Only the current immediately-preceding-origin holder keeps its
            // light-purple marker; every other node settles back to grey.
            Apply(nodeId,
                  (m_mostRecentPastOrigin.has_value() && *m_mostRecentPastOrigin == nodeId)
                      ? VizColor::kPastOrigin
                      : VizColor::kGrey);
            break;
        case VizColor::kPastOrigin:
        case VizColor::kGrey:
            break; // already settled; nothing to do
        }
    }

    void Apply(uint32_t nodeId, VizColor color)
    {
        m_state[nodeId] = color;
        switch (color)
        {
        case VizColor::kGrey:
            m_anim.UpdateNodeSize(nodeId, m_nodeSize, m_nodeSize);
            m_anim.UpdateNodeColor(nodeId, 200, 200, 200);
            break;
        case VizColor::kPaleRed:
            m_anim.UpdateNodeSize(nodeId, m_nodeSize, m_nodeSize);
            m_anim.UpdateNodeColor(nodeId, 255, 170, 170);
            break;
        case VizColor::kRed:
            m_anim.UpdateNodeSize(nodeId, m_nodeSize * 1.4, m_nodeSize * 1.4);
            m_anim.UpdateNodeColor(nodeId, 255, 0, 0);
            break;
        case VizColor::kPastOrigin:
            m_anim.UpdateNodeSize(nodeId, m_nodeSize * 1.6, m_nodeSize * 1.6);
            m_anim.UpdateNodeColor(nodeId, 204, 153, 255); // a past burst's estimated origin: light purple
            break;
        case VizColor::kLatestOrigin:
            m_anim.UpdateNodeSize(nodeId, m_nodeSize * 1.6, m_nodeSize * 1.6);
            m_anim.UpdateNodeColor(nodeId, 160, 32, 240); // this burst's estimated origin: purple
            break;
        }
    }

    static int RankOf(VizColor color)
    {
        return static_cast<int>(color);
    }

    AnimationInterface& m_anim;
    PhysicalRouteIndex& m_routes;
    uint32_t m_victimNodeId;
    double m_nodeSize;
    std::map<uint32_t, VizColor> m_state; // absent == kGrey (matches initial NetAnim coloring)
    std::set<uint32_t> m_pendingReset;
    std::optional<uint32_t> m_mostRecentPastOrigin; // sole holder of the light-purple marker
};

} // namespace

int
main(int argc, char* argv[])
{
    std::string configPath = "config/default.yaml";
    std::string outDir = "results";
    std::string runTag = "";
    int seedOverride = -1;
    int nOverride = -1;

    CommandLine cmd;
    cmd.AddValue("config", "Path to the LiveTrace YAML config", configPath);
    cmd.AddValue("outdir", "Directory for result logs", outDir);
    cmd.AddValue("runTag", "Extra tag appended to output filenames", runTag);
    cmd.AddValue("seed", "Override config seed", seedOverride);
    cmd.AddValue("n", "Override topology.n", nOverride);
    cmd.Parse(argc, argv);

    LiveTraceConfig cfg;
    if (!cfg.Load(configPath))
    {
        std::cerr << "LiveTrace: failed to load config at " << configPath << std::endl;
        return 1;
    }

    uint32_t seed = seedOverride >= 0 ? static_cast<uint32_t>(seedOverride) : static_cast<uint32_t>(cfg.GetInt("seed", 1));
    uint32_t n = nOverride >= 0 ? static_cast<uint32_t>(nOverride) : static_cast<uint32_t>(cfg.GetInt("topology.n", 50));
    // topology.avg_degree, when set (>0), takes priority over topology.p: it
    // derives p = avg_degree/(n-1) so mean node degree -- and thus the mesh's
    // diameter, which grows as ~log N for a fixed degree -- stays constant
    // across an N sweep instead of the graph getting relatively denser (and
    // diameter staying flat) as N grows with a fixed p.
    double avgDegree = cfg.GetDouble("topology.avg_degree", 0.0);
    double p = avgDegree > 0.0 && n > 1 ? avgDegree / static_cast<double>(n - 1) : cfg.GetDouble("topology.p", 0.12);
    uint32_t maxRegen = static_cast<uint32_t>(cfg.GetInt("topology.max_regen_attempts", 50));
    double stopTimeS = cfg.GetDouble("sim.stop_time_s", 900.0);

    RngSeedManager::SetSeed(seed);
    RngSeedManager::SetRun(1);

    std::ostringstream tagStream;
    tagStream << "seed" << seed << "_n" << n;
    if (!runTag.empty())
    {
        tagStream << "_" << runTag;
    }
    std::string tag = tagStream.str();

    std::string oraclePath = outDir + "/oracle_" + tag + ".jsonl";
    std::string observedPath = outDir + "/observed_" + tag + ".jsonl";
    std::string netanimPath = outDir + "/netanim_" + tag + ".xml";
    std::string topologyMapPath = outDir + "/topology_" + tag + ".jsonl";
    std::string tracebackPath = outDir + "/traceback_" + tag + ".jsonl";
    std::string reidPath = outDir + "/reid_" + tag + ".jsonl";

    OracleLogger oracle(oraclePath);
    ObservationLog obsLog(observedPath);

    RandomMeshTopology topology(n, p, maxRegen, seed);
    RandomMeshTopology::BuildResult built = topology.Build();
    oracle.LogTopologyNote(built.note);
    std::cout << "[LiveTrace] " << built.note << std::endl;

    NodeContainer nodes = built.nodes;
    uint32_t victimNodeId = built.victimNodeId;

    LayoutNodesOnCircle(nodes);

    // nodeAddresses[i] is "an" address to dial node i (interface 1 is enough
    // to be reachable via global routing); addrIndex, in contrast, must map
    // *every* address a node owns back to it, because a node with several
    // edges (several interfaces) can send or receive on any of them
    // depending on the route a given packet takes.
    std::vector<Ipv4Address> nodeAddresses(nodes.GetN());
    NodeAddressIndex addrIndex; // public address book (not ground truth): built from
                                // interface assignment, same as a real host inventory.
    {
        std::ofstream topoOut(topologyMapPath);
        for (uint32_t i = 0; i < nodes.GetN(); ++i)
        {
            Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4>();
            uint32_t nIfaces = ipv4 ? ipv4->GetNInterfaces() : 0;
            nodeAddresses[i] = (nIfaces > 1) ? ipv4->GetAddress(1, 0).GetLocal() : Ipv4Address();
            for (uint32_t ifIdx = 1; ifIdx < nIfaces; ++ifIdx)
            {
                Ipv4Address addr = ipv4->GetAddress(ifIdx, 0).GetLocal();
                addrIndex.Register(addr, i);
                std::ostringstream addrStr;
                addr.Print(addrStr);
                topoOut << "{\"node_id\":" << i << ",\"address\":\"" << addrStr.str() << "\"}" << std::endl;
            }
        }
    }

    // NetAnim: constructed early so the traceback observer can drive node
    // colors live as hops are confirmed (qualitative view of Phase 5).
    AnimationInterface anim(netanimPath);
    anim.SetMaxPktsPerTraceFile(500000);
    // NetAnim's built-in default node color is red, which is exactly the color
    // we use to mark a confirmed traceback hop. Repaint every node to a neutral
    // grey first so that hops turning red are actually a visible change.
    // The default node size (1x1 in the ~500-radius ring layout) is far too
    // small to see, so scale it up, shrinking for larger N to avoid overlap.
    double nodeSize = std::max(6.0, std::min(25.0, 1500.0 / nodes.GetN()));
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        anim.UpdateNodeSize(i, nodeSize, nodeSize);
        anim.UpdateNodeColor(i, 200, 200, 200); // idle relay: grey
    }
    anim.UpdateNodeSize(victimNodeId, nodeSize * 1.6, nodeSize * 1.6); // victim: larger
    anim.UpdateNodeColor(victimNodeId, 0, 180, 0); // victim: green

    // Victim's well-known service listener: the endpoint the attacker chain
    // ultimately targets. Runs for the whole simulation.
    Ptr<StepstoneRelayApp> victimSink = CreateObject<StepstoneRelayApp>();
    victimSink->Setup(kVictimPort, &obsLog, victimNodeId, /*isTerminal=*/true);
    nodes.Get(victimNodeId)->AddApplication(victimSink);
    victimSink->SetStartTime(Seconds(0.0));
    victimSink->SetStopTime(Seconds(stopTimeS));

    TimingCorrelator::Config corrCfg;
    corrCfg.bucketS = cfg.GetDouble("correlation.bucket_s", 0.05);
    corrCfg.onThresholdPps = cfg.GetDouble("correlation.on_threshold_pps", 1.0);
    corrCfg.minOnOffTransitions = static_cast<uint32_t>(cfg.GetInt("correlation.min_on_off_transitions", 3));
    corrCfg.maxLagBuckets = cfg.GetInt("correlation.max_lag_buckets", 3);
    TimingCorrelator correlator(&obsLog, corrCfg);

    TracebackObserver::Config obsCfg;
    obsCfg.liveWindowS = cfg.GetDouble("window.live_window_s", 5.0);
    obsCfg.accumulationDelayS = cfg.GetDouble("traceback.accumulation_delay_s", 2.0);
    obsCfg.hopDelayS = cfg.GetDouble("traceback.hop_delay_s", 0.6);
    obsCfg.maxHops = static_cast<uint32_t>(cfg.GetInt("traceback.max_hops", 10));
    obsCfg.scoreThreshold = cfg.GetDouble("correlation.score_threshold", 0.5);
    TracebackObserver observer(&obsLog, &addrIndex, &correlator, victimNodeId, obsCfg, tracebackPath);

    ReidentificationEngine::Config reidCfg;
    reidCfg.periodPriorS = cfg.GetDouble("attacker.period_s", 420.0);
    reidCfg.periodicityToleranceS = cfg.GetDouble("reidentification.periodicity_tolerance_s", 5.0);
    reidCfg.evidenceWindowS = cfg.GetDouble("traceback.accumulation_delay_s", 2.0);
    reidCfg.periodicityWeight = cfg.GetDouble("reidentification.periodicity_weight", 0.40);
    reidCfg.timingWeight = cfg.GetDouble("reidentification.timing_weight", 0.45);
    reidCfg.fingerprintWeight = cfg.GetDouble("reidentification.fingerprint_weight", 0.15);
    reidCfg.clusterScoreThreshold = cfg.GetDouble("reidentification.cluster_score_threshold", 0.5);
    ReidentificationEngine reid(&obsLog, &correlator, reidCfg, reidPath);

    PhysicalRouteIndex physicalRoutes;
    TraceVisualizer viz(anim, physicalRoutes, victimNodeId, nodeSize);

    if (cfg.GetBool("correlation.enabled", true))
    {
        physicalRoutes.Connect();
        victimSink->SetRecvNotify([&observer](uint32_t nodeId, std::string flowKey, double t, Ipv4Address peer) {
            observer.OnFlowObserved(nodeId, flowKey, t, peer);
        });
        observer.SetTraceStartedNotify(
            [&viz](uint32_t traceId, double timeS) { viz.OnTraceStarted(traceId, timeS); });
        observer.SetTraceCompleteNotify([&reid, &viz](uint32_t traceId, double detectTimeS,
                                                        std::vector<uint32_t> chain, std::string stopReason,
                                                        std::string hop0FlowKey, std::string lastConfirmedFlowKey) {
            reid.OnTraceComplete(traceId, detectTimeS, chain, stopReason, hop0FlowKey);
            viz.OnTraceComplete(traceId, chain, stopReason, lastConfirmedFlowKey);
        });
        observer.SetHopConfirmedNotify([&viz](uint32_t traceId, uint32_t nodeId, uint32_t hopsSoFar,
                                               double evalTimeS, std::string downstreamFlowKey) {
            viz.OnHopConfirmed(traceId, nodeId, hopsSoFar, evalTimeS, downstreamFlowKey);
        });
    }

    // Background traffic (ordinary noise, no ground truth). The configured
    // rate is per-node; scale by network size so ambient traffic density at
    // any one node stays roughly constant across the N sweep, rather than
    // thinning out as the same fixed aggregate rate spreads over more nodes.
    BackgroundTraffic::Config bgCfg;
    bgCfg.ratePps = cfg.GetDouble("background.rate_pps", 4.0) * std::max<uint32_t>(1, n);
    bgCfg.packetSizeBytes = static_cast<uint32_t>(cfg.GetInt("background.packet_size_bytes", 512));
    bgCfg.listenPort = kBackgroundPort;
    BackgroundTraffic background(nodes, nodeAddresses, &obsLog, bgCfg, seed * 1000 + 7, stopTimeS);
    if (cfg.GetBool("background.enabled", true))
    {
        background.Start();
    }

    // The attacker campaign(s).
    uint32_t actorCount = static_cast<uint32_t>(cfg.GetInt("attacker.count", 1));
    std::mt19937 phaseRng(seed * 1000 + 13);
    std::uniform_real_distribution<double> phasePick(0.0, cfg.GetDouble("attacker.period_s", 420.0));

    std::vector<std::unique_ptr<AttackerCampaign>> campaigns;
    for (uint32_t a = 0; a < actorCount; ++a)
    {
        AttackerCampaign::Config acfg;
        acfg.actorId = a;
        acfg.periodS = cfg.GetDouble("attacker.period_s", 420.0);
        acfg.phaseOffsetS = phasePick(phaseRng);
        acfg.burstS = cfg.GetDouble("attacker.burst_s", 5.0);
        acfg.chainLenMin = static_cast<uint32_t>(cfg.GetInt("attacker.chain_length_min", 2));
        acfg.chainLenAbsoluteCap = static_cast<uint32_t>(cfg.GetInt("attacker.chain_length_absolute_cap", 40));
        acfg.chainLengthFactor = cfg.GetDouble("attacker.chain_length_factor", 1.2);
        acfg.diameter = built.diameter;
        acfg.packetsPerBurst = static_cast<uint32_t>(cfg.GetInt("attacker.packets_per_burst", 80));
        acfg.packetSizeBytes = static_cast<uint32_t>(cfg.GetInt("background.packet_size_bytes", 512));
        acfg.relayPoolFraction = cfg.GetDouble("attacker.relay_pool_fraction", 0.35);

        auto campaign = std::make_unique<AttackerCampaign>(nodes, victimNodeId, kVictimPort, nodeAddresses, &obsLog,
                                                             &oracle, acfg, seed * 1000 + 100 + a, stopTimeS);
        campaign->Start();
        campaigns.push_back(std::move(campaign));
    }

    Simulator::Stop(Seconds(stopTimeS + 5.0));
    Simulator::Run();
    Simulator::Destroy();

    std::cout << "[LiveTrace] run complete: tag=" << tag << " oracle=" << oraclePath << " observed=" << observedPath
              << " traceback=" << tracebackPath << " reid=" << reidPath << " topology=" << topologyMapPath
              << " netanim=" << netanimPath << std::endl;
    return 0;
}
