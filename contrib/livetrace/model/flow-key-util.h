#ifndef LIVETRACE_FLOW_KEY_UTIL_H
#define LIVETRACE_FLOW_KEY_UTIL_H

#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-address.h"

#include <cstdint>
#include <sstream>
#include <string>

namespace ns3
{
namespace livetrace
{

/**
 * Flow keys are built only from locally-visible 5-tuple-like information
 * (an address and port either side already knows about its own socket, or
 * learns from the peer address on a received packet) -- never from any
 * burst/actor/chain identifier. This is what keeps the observation log a
 * faithful stand-in for what a real network monitor could see.
 */
inline std::string
AddrPortStr(Ipv4Address addr, uint16_t port)
{
    std::ostringstream oss;
    addr.Print(oss);
    oss << ":" << port;
    return oss.str();
}

inline std::string
MakeFlowKey(Ipv4Address srcAddr, uint16_t srcPort, Ipv4Address dstAddr, uint16_t dstPort)
{
    return AddrPortStr(srcAddr, srcPort) + "->" + AddrPortStr(dstAddr, dstPort);
}

} // namespace livetrace
} // namespace ns3

#endif // LIVETRACE_FLOW_KEY_UTIL_H
