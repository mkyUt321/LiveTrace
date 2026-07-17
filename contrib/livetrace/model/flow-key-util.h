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

/// Parses the "<addr>:<port>" side (src, before "->", or dst, after) back
/// out of a flow key. This is just undoing our own string formatting, not
/// deep packet inspection -- the equivalent of a monitor reading the 5-tuple
/// off a captured packet.
inline Ipv4Address
FlowKeySideAddr(const std::string& flowKey, bool wantSrc)
{
    size_t arrow = flowKey.find("->");
    if (arrow == std::string::npos)
    {
        return Ipv4Address();
    }
    std::string side = wantSrc ? flowKey.substr(0, arrow) : flowKey.substr(arrow + 2);
    size_t colon = side.rfind(':');
    if (colon == std::string::npos)
    {
        return Ipv4Address();
    }
    return Ipv4Address(side.substr(0, colon).c_str());
}

} // namespace livetrace
} // namespace ns3

#endif // LIVETRACE_FLOW_KEY_UTIL_H
