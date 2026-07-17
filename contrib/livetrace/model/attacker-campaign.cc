#include "attacker-campaign.h"

#include "flow-key-util.h"
#include "stepstone-relay-app.h"

#include "ns3/inet-socket-address.h"
#include "ns3/simulator.h"
#include "ns3/udp-socket-factory.h"

#include <algorithm>

namespace ns3
{
namespace livetrace
{

namespace
{
// Shared across all AttackerCampaign instances (single-threaded ns-3 event
// loop, so a plain counter is safe): unique burst ids, and a rolling
// ephemeral-port cursor for per-hop relay listen ports.
uint32_t g_nextBurstId = 0;
uint32_t g_nextRelayPort = 20000;

uint16_t
AllocateRelayPort()
{
    uint16_t port = static_cast<uint16_t>(g_nextRelayPort);
    g_nextRelayPort++;
    if (g_nextRelayPort > 65000)
    {
        g_nextRelayPort = 20000;
    }
    return port;
}
} // namespace

AttackerCampaign::AttackerCampaign(NodeContainer nodes,
                                    uint32_t victimNodeId,
                                    uint16_t victimPort,
                                    std::vector<Ipv4Address> nodeAddresses,
                                    ObservationLog* obsLog,
                                    OracleLogger* oracle,
                                    Config cfg,
                                    uint32_t rngSeed,
                                    double stopTimeS)
    : m_nodes(nodes),
      m_victimNodeId(victimNodeId),
      m_victimPort(victimPort),
      m_nodeAddresses(std::move(nodeAddresses)),
      m_obsLog(obsLog),
      m_oracle(oracle),
      m_cfg(cfg),
      m_rng(rngSeed),
      m_stopTimeS(stopTimeS)
{
    std::vector<uint32_t> candidates;
    for (uint32_t i = 0; i < m_nodes.GetN(); ++i)
    {
        if (i != m_victimNodeId)
        {
            candidates.push_back(i);
        }
    }
    std::shuffle(candidates.begin(), candidates.end(), m_rng);
    double fraction = (m_cfg.relayPoolFraction > 0.0) ? m_cfg.relayPoolFraction : 1.0;
    uint32_t poolSize = std::max<uint32_t>(1, static_cast<uint32_t>(candidates.size() * fraction));
    m_relayPool.assign(candidates.begin(), candidates.begin() + std::min<size_t>(poolSize, candidates.size()));
}

void
AttackerCampaign::Start()
{
    Simulator::Schedule(Seconds(m_cfg.phaseOffsetS), &AttackerCampaign::FireBurst, this);
}

void
AttackerCampaign::FireBurst()
{
    double now = Simulator::Now().GetSeconds();
    if (now > m_stopTimeS)
    {
        return;
    }

    uint32_t n = m_nodes.GetN();
    std::uniform_int_distribution<uint32_t> nodePick(0, n - 1);

    // Pick a fresh random origin (never the victim).
    uint32_t origin;
    do
    {
        origin = nodePick(m_rng);
    } while (origin == m_victimNodeId);

    // Pick a fresh random chain of k distinct relays, drawn from this actor's
    // own private relay pool (never origin or victim) -- origin itself stays
    // freely chosen from the whole network every period, per spec.
    std::uniform_int_distribution<uint32_t> kPick(m_cfg.chainLenMin, m_cfg.chainLenMax);
    uint32_t k = kPick(m_rng);
    k = std::min<uint32_t>(k, static_cast<uint32_t>(m_relayPool.size()));

    std::vector<uint32_t> chain;
    chain.push_back(origin);
    std::vector<uint32_t> used = {origin, m_victimNodeId};
    std::uniform_int_distribution<uint32_t> poolPick(0, m_relayPool.empty() ? 0 : m_relayPool.size() - 1);
    for (uint32_t i = 0; i < k; ++i)
    {
        uint32_t relay;
        uint32_t attempts = 0;
        do
        {
            relay = m_relayPool[poolPick(m_rng)];
            attempts++;
        } while (std::find(used.begin(), used.end(), relay) != used.end() && attempts < 1000);
        if (attempts >= 1000)
        {
            break; // pool too small for this chain length; take what we have
        }
        chain.push_back(relay);
        used.push_back(relay);
    }
    chain.push_back(m_victimNodeId);

    uint32_t burstId = g_nextBurstId++;

    // Assign a fresh listen port to every intermediate relay hop (index 1..chain.size()-2).
    std::vector<uint16_t> hopPort(chain.size(), 0);
    for (size_t i = 1; i + 1 < chain.size(); ++i)
    {
        hopPort[i] = AllocateRelayPort();
    }

    double stopSlack = 2.0;
    for (size_t i = 1; i + 1 < chain.size(); ++i)
    {
        uint32_t nodeId = chain[i];
        uint16_t localPort = hopPort[i];
        bool lastHop = (i + 2 == chain.size());
        Ipv4Address fwdAddr = m_nodeAddresses[chain[i + 1]];
        uint16_t fwdPort = lastHop ? m_victimPort : hopPort[i + 1];

        Ptr<StepstoneRelayApp> app = CreateObject<StepstoneRelayApp>();
        app->Setup(localPort, m_obsLog, nodeId, false, fwdAddr, fwdPort);
        m_nodes.Get(nodeId)->AddApplication(app);
        // NB: Application::DoInitialize() schedules Start/Stop as *delays from
        // whenever Initialize() runs* (Simulator::Schedule(m_startTime, ...)),
        // not as absolute times. Node::AddApplication schedules Initialize()
        // at +0 from now, so "start immediately" is SetStartTime(0), not
        // SetStartTime(Simulator::Now()) -- the latter double-counts Now().
        app->SetStartTime(Seconds(0.0));
        app->SetStopTime(Seconds(m_cfg.burstS + stopSlack));
    }

    // Origin sends the burst as a handful of ON sub-bursts separated by idle
    // gaps, giving the timing correlator an on/off pattern to work with.
    Ipv4Address firstHopAddr = (chain.size() > 2) ? m_nodeAddresses[chain[1]] : m_nodeAddresses[m_victimNodeId];
    uint16_t firstHopPort = (chain.size() > 2) ? hopPort[1] : m_victimPort;

    Ptr<Socket> sock = Socket::CreateSocket(m_nodes.Get(origin), UdpSocketFactory::GetTypeId());
    sock->Bind();
    sock->Connect(InetSocketAddress(firstHopAddr, firstHopPort));
    m_activeSockets.push_back(sock);

    Address boundAddr;
    sock->GetSockName(boundAddr);
    uint16_t originLocalPort = InetSocketAddress::ConvertFrom(boundAddr).GetPort();
    std::string originFlowKey = MakeFlowKey(m_nodeAddresses[origin], originLocalPort, firstHopAddr, firstHopPort);

    // Sub-burst (ON) / idle-gap (OFF) cadence: deliberately short relative to
    // the live window so a handful of on/off transitions -- what timing
    // correlation actually needs -- show up within the first second or two
    // of the burst, not only once the whole burst_s has elapsed.
    std::uniform_int_distribution<uint32_t> subBurstCountPick(5, 8);
    uint32_t numSubBursts = std::min(subBurstCountPick(m_rng), m_cfg.packetsPerBurst > 0 ? m_cfg.packetsPerBurst : 1);
    numSubBursts = std::max<uint32_t>(numSubBursts, 1);
    uint32_t packetsPerSub = std::max<uint32_t>(3, m_cfg.packetsPerBurst / numSubBursts);
    uint32_t remainder = m_cfg.packetsPerBurst > packetsPerSub * numSubBursts
                             ? m_cfg.packetsPerBurst - packetsPerSub * numSubBursts
                             : 0;

    double intraGapS = 0.01; // 10ms between packets within one ON interval
    std::uniform_real_distribution<double> idleGapPick(0.15, 0.4);

    double t = 0.0;
    for (uint32_t s = 0; s < numSubBursts; ++s)
    {
        uint32_t countThisSub = packetsPerSub + (s == 0 ? remainder : 0);
        Simulator::Schedule(Seconds(t), &AttackerCampaign::SendSubBurstPackets, this, sock, countThisSub,
                             intraGapS, m_cfg.packetSizeBytes, origin, originFlowKey);
        t += countThisSub * intraGapS + idleGapPick(m_rng);
    }

    Simulator::Schedule(Seconds(m_cfg.burstS + stopSlack), [this, sock]() {
        auto it = std::find(m_activeSockets.begin(), m_activeSockets.end(), sock);
        if (it != m_activeSockets.end())
        {
            (*it)->Close();
            m_activeSockets.erase(it);
        }
    });

    if (m_oracle)
    {
        m_oracle->LogBurst(burstId, m_cfg.actorId, now, chain);
    }

    if (now + m_cfg.periodS <= m_stopTimeS)
    {
        Simulator::Schedule(Seconds(m_cfg.periodS), &AttackerCampaign::FireBurst, this);
    }
}

void
AttackerCampaign::SendSubBurstPackets(Ptr<Socket> sock,
                                       uint32_t count,
                                       double intraGapS,
                                       uint32_t sizeBytes,
                                       uint32_t originNodeId,
                                       std::string flowKey)
{
    ObservationLog* obsLog = m_obsLog;
    for (uint32_t i = 0; i < count; ++i)
    {
        Simulator::Schedule(Seconds(i * intraGapS), [sock, sizeBytes, obsLog, originNodeId, flowKey]() {
            Ptr<Packet> p = Create<Packet>(sizeBytes);
            sock->Send(p);
            if (obsLog)
            {
                obsLog->RecordSend(originNodeId, flowKey, Simulator::Now().GetSeconds(), sizeBytes);
            }
        });
    }
}

} // namespace livetrace
} // namespace ns3
