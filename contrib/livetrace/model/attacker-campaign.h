#ifndef LIVETRACE_ATTACKER_CAMPAIGN_H
#define LIVETRACE_ATTACKER_CAMPAIGN_H

#include "observation-log.h"
#include "oracle-logger.h"

#include "ns3/ipv4-address.h"
#include "ns3/node-container.h"
#include "ns3/socket.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace ns3
{
namespace livetrace
{

/**
 * One moving-origin periodic attacker ("actor"). Every period_s it fires a
 * burst_s-long burst from a freshly-chosen random origin node, through a
 * freshly-chosen random chain of stepping-stone relays, to the fixed victim.
 * Only this class and OracleLogger know the true actor id and chain; it
 * writes that to OracleLogger and nowhere else. All wire traffic it
 * generates is logged only to ObservationLog, exactly like real traffic
 * would be seen by a network monitor.
 */
class AttackerCampaign
{
  public:
    struct Config
    {
        uint32_t actorId;
        double periodS;
        double phaseOffsetS;
        double burstS;
        uint32_t chainLenMin;
        uint32_t chainLenMax;
        uint32_t packetsPerBurst;
        uint32_t packetSizeBytes;
    };

    AttackerCampaign(NodeContainer nodes,
                      uint32_t victimNodeId,
                      uint16_t victimPort,
                      std::vector<Ipv4Address> nodeAddresses,
                      ObservationLog* obsLog,
                      OracleLogger* oracle,
                      Config cfg,
                      uint32_t rngSeed,
                      double stopTimeS);

    /// Schedules the first burst (at phaseOffsetS) and, from within each
    /// burst, the next one -- until stopTimeS.
    void Start();

  private:
    void FireBurst();
    void SendSubBurstPackets(Ptr<Socket> sock,
                              uint32_t count,
                              double intraGapS,
                              uint32_t sizeBytes,
                              uint32_t originNodeId,
                              std::string flowKey);

    NodeContainer m_nodes;
    uint32_t m_victimNodeId;
    uint16_t m_victimPort;
    std::vector<Ipv4Address> m_nodeAddresses;
    ObservationLog* m_obsLog;
    OracleLogger* m_oracle;
    Config m_cfg;
    std::mt19937 m_rng;
    double m_stopTimeS;
    std::vector<Ptr<Socket>> m_activeSockets;
};

} // namespace livetrace
} // namespace ns3

#endif // LIVETRACE_ATTACKER_CAMPAIGN_H
