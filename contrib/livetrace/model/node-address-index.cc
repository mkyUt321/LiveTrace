#include "node-address-index.h"

namespace ns3
{
namespace livetrace
{

void
NodeAddressIndex::Register(Ipv4Address addr, uint32_t nodeId)
{
    m_map[addr.Get()] = nodeId;
}

uint32_t
NodeAddressIndex::Lookup(Ipv4Address addr) const
{
    auto it = m_map.find(addr.Get());
    if (it == m_map.end())
    {
        return kNotFound;
    }
    return it->second;
}

} // namespace livetrace
} // namespace ns3
