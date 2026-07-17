// LiveTrace Phase 0 simulation driver.
//
// Builds a random relay mesh, runs one moving-origin periodic attacker
// (stepping-stone chains) plus ordinary background traffic against a fixed
// victim, with ground truth recorded only to the oracle log and observable
// traffic recorded only to the observation log. No correlation/traceback
// runs yet (added in later phases) -- this is the "correlation OFF" sanity
// build called for at the end of Phase 0.

#include "ns3/animation-interface.h"
#include "ns3/attacker-campaign.h"
#include "ns3/background-traffic.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/livetrace-config.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/observation-log.h"
#include "ns3/oracle-logger.h"
#include "ns3/random-mesh-topology.h"
#include "ns3/stepstone-relay-app.h"

#include <cmath>
#include <memory>
#include <random>
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
    double p = cfg.GetDouble("topology.p", 0.12);
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

    OracleLogger oracle(oraclePath);
    ObservationLog obsLog(observedPath);

    RandomMeshTopology topology(n, p, maxRegen, seed);
    RandomMeshTopology::BuildResult built = topology.Build();
    oracle.LogTopologyNote(built.note);
    std::cout << "[LiveTrace] " << built.note << std::endl;

    NodeContainer nodes = built.nodes;
    uint32_t victimNodeId = built.victimNodeId;

    LayoutNodesOnCircle(nodes);

    std::vector<Ipv4Address> nodeAddresses(nodes.GetN());
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4>();
        nodeAddresses[i] = (ipv4 && ipv4->GetNInterfaces() > 1) ? ipv4->GetAddress(1, 0).GetLocal() : Ipv4Address();
    }

    // Victim's well-known service listener: the endpoint the attacker chain
    // ultimately targets. Runs for the whole simulation.
    Ptr<StepstoneRelayApp> victimSink = CreateObject<StepstoneRelayApp>();
    victimSink->Setup(kVictimPort, &obsLog, victimNodeId, /*isTerminal=*/true);
    nodes.Get(victimNodeId)->AddApplication(victimSink);
    victimSink->SetStartTime(Seconds(0.0));
    victimSink->SetStopTime(Seconds(stopTimeS));

    // Background traffic (ordinary noise, no ground truth).
    BackgroundTraffic::Config bgCfg;
    bgCfg.ratePps = cfg.GetDouble("background.rate_pps", 4.0);
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
        acfg.chainLenMax = static_cast<uint32_t>(cfg.GetInt("attacker.chain_length_max", 5));
        acfg.packetsPerBurst = static_cast<uint32_t>(cfg.GetInt("attacker.packets_per_burst", 40));
        acfg.packetSizeBytes = static_cast<uint32_t>(cfg.GetInt("background.packet_size_bytes", 512));

        auto campaign = std::make_unique<AttackerCampaign>(nodes, victimNodeId, kVictimPort, nodeAddresses, &obsLog,
                                                             &oracle, acfg, seed * 1000 + 100 + a, stopTimeS);
        campaign->Start();
        campaigns.push_back(std::move(campaign));
    }

    AnimationInterface anim(netanimPath);
    anim.SetMaxPktsPerTraceFile(500000);

    Simulator::Stop(Seconds(stopTimeS + 5.0));
    Simulator::Run();
    Simulator::Destroy();

    std::cout << "[LiveTrace] run complete: tag=" << tag << " oracle=" << oraclePath << " observed=" << observedPath
              << " netanim=" << netanimPath << std::endl;
    return 0;
}
