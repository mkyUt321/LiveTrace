#include "stepstone-relay-app.h"

#include "flow-key-util.h"

#include "ns3/double.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/udp-socket-factory.h"

namespace ns3
{
namespace livetrace
{

NS_LOG_COMPONENT_DEFINE("LiveTraceStepstoneRelayApp");

namespace
{

Ipv4Address
GetNodeIpv4Address(Ptr<Node> node)
{
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    // Interface 0 is loopback; interface 1 is the first real link.
    if (ipv4 && ipv4->GetNInterfaces() > 1)
    {
        return ipv4->GetAddress(1, 0).GetLocal();
    }
    return Ipv4Address();
}

} // namespace

TypeId
StepstoneRelayApp::GetTypeId()
{
    static TypeId tid = TypeId("ns3::livetrace::StepstoneRelayApp")
                             .SetParent<Application>()
                             .SetGroupName("LiveTrace")
                             .AddConstructor<StepstoneRelayApp>();
    return tid;
}

StepstoneRelayApp::StepstoneRelayApp()
    : m_localPort(0),
      m_sendLocalPort(0),
      m_obsLog(nullptr),
      m_nodeId(0),
      m_isTerminal(true),
      m_forwardPort(0)
{
    m_relayDelay = CreateObject<UniformRandomVariable>();
    m_relayDelay->SetAttribute("Min", DoubleValue(0.001));
    m_relayDelay->SetAttribute("Max", DoubleValue(0.015));
}

StepstoneRelayApp::~StepstoneRelayApp()
{
}

void
StepstoneRelayApp::Setup(uint16_t localPort,
                          ObservationLog* obsLog,
                          uint32_t nodeId,
                          bool isTerminal,
                          Ipv4Address forwardAddr,
                          uint16_t forwardPort)
{
    m_localPort = localPort;
    m_obsLog = obsLog;
    m_nodeId = nodeId;
    m_isTerminal = isTerminal;
    m_forwardAddr = forwardAddr;
    m_forwardPort = forwardPort;
}

void
StepstoneRelayApp::StartApplication()
{
    if (!m_recvSocket)
    {
        m_recvSocket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
        InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), m_localPort);
        m_recvSocket->Bind(local);
        m_recvSocket->SetRecvCallback(MakeCallback(&StepstoneRelayApp::HandleRead, this));
    }
    if (!m_isTerminal && !m_sendSocket)
    {
        m_sendSocket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
        m_sendSocket->Bind();
        m_sendSocket->Connect(InetSocketAddress(m_forwardAddr, m_forwardPort));
        Address boundAddr;
        m_sendSocket->GetSockName(boundAddr);
        m_sendLocalPort = InetSocketAddress::ConvertFrom(boundAddr).GetPort();
    }
}

void
StepstoneRelayApp::StopApplication()
{
    if (m_recvSocket)
    {
        m_recvSocket->Close();
        m_recvSocket = nullptr;
    }
    if (m_sendSocket)
    {
        m_sendSocket->Close();
        m_sendSocket = nullptr;
    }
}

void
StepstoneRelayApp::HandleRead(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;
    while ((packet = socket->RecvFrom(from)))
    {
        if (packet->GetSize() == 0)
        {
            break;
        }
        InetSocketAddress peer = InetSocketAddress::ConvertFrom(from);
        Ipv4Address peerAddr = peer.GetIpv4();
        uint16_t peerPort = peer.GetPort();

        if (m_obsLog)
        {
            std::string flowKeyIn = MakeFlowKey(peerAddr, peerPort, GetNodeIpv4Address(GetNode()), m_localPort);
            m_obsLog->RecordRecv(m_nodeId, flowKeyIn, Simulator::Now().GetSeconds(), packet->GetSize());
        }

        if (!m_isTerminal)
        {
            DoForward(packet, peerAddr, peerPort);
        }
    }
}

void
StepstoneRelayApp::DoForward(Ptr<Packet> packet, Ipv4Address peerAddr, uint16_t peerPort)
{
    uint32_t size = packet->GetSize();
    double delay = m_relayDelay->GetValue();
    Simulator::Schedule(Seconds(delay), [this, size]() {
        if (!m_sendSocket)
        {
            return;
        }
        Ptr<Packet> fwd = Create<Packet>(size);
        m_sendSocket->Send(fwd);
        if (m_obsLog)
        {
            Ipv4Address localAddr = GetNodeIpv4Address(GetNode());
            std::string flowKeyOut = MakeFlowKey(localAddr, m_sendLocalPort, m_forwardAddr, m_forwardPort);
            m_obsLog->RecordSend(m_nodeId, flowKeyOut, Simulator::Now().GetSeconds(), size);
        }
    });
}

} // namespace livetrace
} // namespace ns3
