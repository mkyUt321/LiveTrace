#ifndef LIVETRACE_STEPSTONE_RELAY_APP_H
#define LIVETRACE_STEPSTONE_RELAY_APP_H

#include "observation-log.h"

#include "ns3/application.h"
#include "ns3/ipv4-address.h"
#include "ns3/ptr.h"
#include "ns3/random-variable-stream.h"
#include "ns3/socket.h"

#include <functional>
#include <string>

namespace ns3
{
namespace livetrace
{

/**
 * A stepping-stone relay: listens on one UDP port, and immediately forwards
 * whatever it receives (after a small per-hop relay delay) to a configured
 * downstream address:port, preserving the inbound packet's relative
 * on/off timing -- the assumption Zhang-Paxson style correlation relies on.
 *
 * With no forward target configured it behaves as a terminal sink (used for
 * the victim's well-known listening service): it only records arrivals.
 *
 * Only records to ObservationLog (observable traffic). Never touches
 * OracleLogger -- this class has no idea it is part of an attack chain.
 */
class StepstoneRelayApp : public Application
{
  public:
    /// nodeId, flowKey, timeS, peerAddr (the sender's address for this packet).
    using RecvNotifyFn = std::function<void(uint32_t, std::string, double, Ipv4Address)>;

    static TypeId GetTypeId();
    StepstoneRelayApp();
    ~StepstoneRelayApp() override;

    /// isTerminal = true => sink only, no forwarding (used for the victim).
    void Setup(uint16_t localPort,
               ObservationLog* obsLog,
               uint32_t nodeId,
               bool isTerminal,
               Ipv4Address forwardAddr = Ipv4Address(),
               uint16_t forwardPort = 0);

    /// Optional hook fired synchronously on every packet arrival (after it
    /// is logged to ObservationLog). Used by the traceback observer to know
    /// when a new flow starts at a node it is watching.
    void SetRecvNotify(RecvNotifyFn fn);

  private:
    void StartApplication() override;
    void StopApplication() override;
    void HandleRead(Ptr<Socket> socket);
    void DoForward(Ptr<Packet> packet, Ipv4Address peerAddr, uint16_t peerPort);

    Ptr<Socket> m_recvSocket;
    Ptr<Socket> m_sendSocket;
    uint16_t m_localPort;
    uint16_t m_sendLocalPort;
    ObservationLog* m_obsLog;
    uint32_t m_nodeId;
    bool m_isTerminal;
    Ipv4Address m_forwardAddr;
    uint16_t m_forwardPort;
    Ptr<UniformRandomVariable> m_relayDelay;
    RecvNotifyFn m_recvNotify;
};

} // namespace livetrace
} // namespace ns3

#endif // LIVETRACE_STEPSTONE_RELAY_APP_H
