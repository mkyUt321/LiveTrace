#ifndef LIVETRACE_BACKGROUND_TRAFFIC_H
#define LIVETRACE_BACKGROUND_TRAFFIC_H

#include "observation-log.h"

#include "ns3/ipv4-address.h"
#include "ns3/node-container.h"
#include "ns3/socket.h"

#include <cstdint>
#include <random>
#include <vector>

namespace ns3
{
namespace livetrace
{

/**
 * Ordinary background chatter: random node pairs exchange short UDP
 * sessions at a Poisson arrival rate. Generates plausible noise flows for
 * the correlator to reject -- it never touches OracleLogger and carries no
 * "this is not an attack" label; the re-identification/correlation stack
 * has to tell it apart from real chain traffic using only timing/behavior.
 */
class BackgroundTraffic
{
  public:
    struct Config
    {
        double ratePps;        // aggregate arrival rate of new sessions
        uint32_t packetSizeBytes;
        uint16_t listenPort;
    };

    BackgroundTraffic(NodeContainer nodes,
                       std::vector<Ipv4Address> nodeAddresses,
                       ObservationLog* obsLog,
                       Config cfg,
                       uint32_t rngSeed,
                       double stopTimeS);

    /// Installs a sink app on every node, then starts scheduling sessions.
    void Start();

  private:
    void ScheduleNextArrival();
    void FireSession();

    NodeContainer m_nodes;
    std::vector<Ipv4Address> m_nodeAddresses;
    ObservationLog* m_obsLog;
    Config m_cfg;
    std::mt19937 m_rng;
    double m_stopTimeS;
    std::vector<Ptr<Socket>> m_activeSockets;
};

} // namespace livetrace
} // namespace ns3

#endif // LIVETRACE_BACKGROUND_TRAFFIC_H
