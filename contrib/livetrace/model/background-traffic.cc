#include "background-traffic.h"

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

BackgroundTraffic::BackgroundTraffic(NodeContainer nodes,
                                      std::vector<Ipv4Address> nodeAddresses,
                                      ObservationLog* obsLog,
                                      Config cfg,
                                      uint32_t rngSeed,
                                      double stopTimeS)
    : m_nodes(nodes),
      m_nodeAddresses(std::move(nodeAddresses)),
      m_obsLog(obsLog),
      m_cfg(cfg),
      m_rng(rngSeed),
      m_stopTimeS(stopTimeS)
{
}

void
BackgroundTraffic::Start()
{
    for (uint32_t i = 0; i < m_nodes.GetN(); ++i)
    {
        Ptr<StepstoneRelayApp> sink = CreateObject<StepstoneRelayApp>();
        sink->Setup(m_cfg.listenPort, m_obsLog, i, /*isTerminal=*/true);
        m_nodes.Get(i)->AddApplication(sink);
        sink->SetStartTime(Seconds(0.0));
        sink->SetStopTime(Seconds(m_stopTimeS));
    }
    ScheduleNextArrival();
}

void
BackgroundTraffic::ScheduleNextArrival()
{
    std::exponential_distribution<double> interArrival(m_cfg.ratePps);
    double gap = interArrival(m_rng);
    Simulator::Schedule(Seconds(gap), &BackgroundTraffic::FireSession, this);
}

void
BackgroundTraffic::FireSession()
{
    double now = Simulator::Now().GetSeconds();
    if (now > m_stopTimeS)
    {
        return;
    }

    uint32_t n = m_nodes.GetN();
    std::uniform_int_distribution<uint32_t> nodePick(0, n - 1);
    uint32_t src = nodePick(m_rng);
    uint32_t dst;
    do
    {
        dst = nodePick(m_rng);
    } while (dst == src);

    Ptr<Socket> sock = Socket::CreateSocket(m_nodes.Get(src), UdpSocketFactory::GetTypeId());
    sock->Bind();
    sock->Connect(InetSocketAddress(m_nodeAddresses[dst], m_cfg.listenPort));
    m_activeSockets.push_back(sock);

    Address boundAddr;
    sock->GetSockName(boundAddr);
    uint16_t localPort = InetSocketAddress::ConvertFrom(boundAddr).GetPort();
    std::string flowKey = MakeFlowKey(m_nodeAddresses[src], localPort, m_nodeAddresses[dst], m_cfg.listenPort);

    std::uniform_int_distribution<uint32_t> packetCountPick(1, 5);
    uint32_t count = packetCountPick(m_rng);
    ObservationLog* obsLog = m_obsLog;
    for (uint32_t i = 0; i < count; ++i)
    {
        Simulator::Schedule(Seconds(i * 0.02), [sock, obsLog, src, flowKey, this]() {
            Ptr<Packet> p = Create<Packet>(m_cfg.packetSizeBytes);
            sock->Send(p);
            if (obsLog)
            {
                obsLog->RecordSend(src, flowKey, Simulator::Now().GetSeconds(), m_cfg.packetSizeBytes);
            }
        });
    }

    Simulator::Schedule(Seconds(count * 0.02 + 0.5), [this, sock]() {
        auto it = std::find(m_activeSockets.begin(), m_activeSockets.end(), sock);
        if (it != m_activeSockets.end())
        {
            (*it)->Close();
            m_activeSockets.erase(it);
        }
    });

    ScheduleNextArrival();
}

} // namespace livetrace
} // namespace ns3
