#ifndef LIVETRACE_NODE_ADDRESS_INDEX_H
#define LIVETRACE_NODE_ADDRESS_INDEX_H

#include "ns3/ipv4-address.h"

#include <cstdint>
#include <limits>
#include <map>

namespace ns3
{
namespace livetrace
{

/**
 * Plain address-book: which physical node owns which IP address. Built once
 * from public topology/interface assignment (the equivalent of a host
 * inventory or ARP table a real operator would have) -- not derived from or
 * dependent on any attack ground truth. Used by the traceback observer to
 * know which node's ObservationLog to search next once it has identified an
 * upstream peer address by timing correlation.
 */
class NodeAddressIndex
{
  public:
    static constexpr uint32_t kNotFound = std::numeric_limits<uint32_t>::max();

    void Register(Ipv4Address addr, uint32_t nodeId);
    uint32_t Lookup(Ipv4Address addr) const;

  private:
    std::map<uint32_t, uint32_t> m_map; // Ipv4Address::Get() -> nodeId
};

} // namespace livetrace
} // namespace ns3

#endif // LIVETRACE_NODE_ADDRESS_INDEX_H
